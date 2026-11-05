#pragma once

#include "s2a/arduino_cli.hpp"
#include "s2a/blocks.hpp"
#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/project.hpp"
#include "s2a/result.hpp"
#include "s2a/serial.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

/// Live mode: the computer runs the blocks and drives the board through the S2A live firmware
/// (firmware/s2a_live), so that a change in the blocks takes effect without uploading.
namespace s2a::live {

inline constexpr int kBaud = 115200;
inline constexpr std::string_view kHello = "S2A 2"; ///< Changes with the protocol: an older firmware is uploaded again.

enum class PinMode { Input, InputPullup, Output };

/// What the interpreter needs from a board. LiveLink talks to the firmware; tests use fakes.
class Pins {
public:
    Pins() = default;
    Pins(const Pins&) = delete;
    Pins& operator=(const Pins&) = delete;
    Pins(Pins&&) = delete;
    Pins& operator=(Pins&&) = delete;
    virtual ~Pins() = default;

    virtual void setMode(int pin, PinMode mode) = 0;
    virtual void digitalWrite(int pin, bool high) = 0;
    virtual void analogWrite(int pin, int value) = 0;
    [[nodiscard]] virtual bool digitalRead(int pin) = 0;
    [[nodiscard]] virtual int analogRead(int pin) = 0;
    /// Sound on a buzzer for `milliseconds` (0: until stopped); a frequency of 0 stops it.
    virtual void tone(int pin, int frequency, int milliseconds) = 0;
    /// Angle (0-180) of a servo.
    virtual void servoWrite(int pin, int angle) = 0;
    /// Distance in centimetres measured by an HC-SR04 ultrasonic sensor (400 when nothing answers).
    [[nodiscard]] virtual double distance(int trigPin, int echoPin) = 0;
    /// Every output low, no sound, servos released (Live mode stopped).
    virtual void reset() = 0;
};

/// Pins of a board running the S2A live firmware, over a line transport.
class LiveLink final : public Pins {
public:
    explicit LiveLink(serial::LineTransport& transport) : transport_(transport) {}

    /// Waits until the firmware greets and answers (after the reset caused by opening the port).
    [[nodiscard]] Status handshake(std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));

    void setMode(int pin, PinMode mode) override;
    void digitalWrite(int pin, bool high) override;
    void analogWrite(int pin, int value) override;
    [[nodiscard]] bool digitalRead(int pin) override;
    [[nodiscard]] int analogRead(int pin) override;
    void tone(int pin, int frequency, int milliseconds) override;
    void servoWrite(int pin, int angle) override;
    [[nodiscard]] double distance(int trigPin, int echoPin) override;
    void reset() override;

    /// First communication error since the last call (the board was unplugged...), if any.
    [[nodiscard]] std::optional<Error> takeError();

private:
    /// True when the firmware answers a "?" now (false while the board restarts).
    [[nodiscard]] Result<bool> confirm();
    /// Sends `command` and returns the reply line ("ok", "r 2 1"...), or nullopt on error.
    std::optional<std::string> request(const std::string& command);

    serial::LineTransport& transport_;
    std::optional<Error> error_;
};

/// Runs the scripts of a project, Scratch style: every script started by a hat runs in turn,
/// a script gives way at each wait and at the end of each loop turn.
class Interpreter {
public:
    using Clock = std::chrono::milliseconds; ///< Time since the start of the Live mode.

    Interpreter(project::Project project, const BlockCatalog& catalog, const Board& board, Pins& pins);

    /// Runs every script until it waits; call it often (every 10 ms or so).
    void step(Clock now);

    /// Takes a new version of the blocks: running scripts continue where they are when their
    /// current block still exists, variables keep their values.
    void update(project::Project project);

    [[nodiscard]] bool finished() const;
    /// Block being run by each running script (to highlight it).
    [[nodiscard]] std::vector<std::string> runningBlocks() const;
    /// Variable id -> current value.
    [[nodiscard]] const std::map<std::string, double>& variables() const { return variables_; }
    /// Pin label ("A0", "2") -> last value read by the blocks (0/1 for digital pins).
    [[nodiscard]] const std::map<std::string, double>& readings() const { return readings_; }
    /// Lines written by "write to the serial monitor" since the last call.
    [[nodiscard]] std::vector<std::string> takeOutput();

private:
    struct Frame {
        const project::Stack* stack = nullptr;
        std::size_t index = 0;
        std::string owner;  ///< Loop block repeating this stack ("" for if/else or the script).
        std::string parent; ///< Block whose mouth holds the stack ("" for the script itself)...
        std::string slot;   ///< ...and the name of that mouth: used to find it again after an update.
        long remaining = 0; ///< Turns left for "repeat"; -1 forever.
    };
    struct Thread {
        std::string hat;
        std::vector<Frame> frames;
        Clock wakeAt{0};
        std::string current;
        bool done = false;
    };

    void start();
    void run(Thread& thread, Clock now);
    double number(const project::Block& block, const std::string& name);
    bool condition(const project::Block& block, const std::string& name);
    double evaluate(const project::Block& block);
    int pin(const project::Block& block, const std::string& name, PinMode mode);
    /// Pin number of a pin menu, without changing its mode (-1 when invalid).
    [[nodiscard]] int pinNumber(const project::Block& block, const std::string& name) const;
    void record(int pin, double value);
    [[nodiscard]] std::string field(const project::Block& block, const std::string& name) const;
    bool restore(Thread& thread, const std::vector<Frame>& old);

    project::Project project_;
    const BlockCatalog& catalog_;
    const Board& board_;
    Pins& pins_;
    std::vector<Thread> threads_;
    std::map<std::string, double> variables_;
    std::map<int, PinMode> modes_;
    std::map<std::string, double> readings_;
    std::vector<std::string> output_;
    std::mt19937 random_{std::random_device{}()};
};

/// Live mode as the editors use it: connects to the board (uploading the firmware when it does
/// not answer), then runs the interpreter on a background thread. The editor sends new versions
/// of the blocks and reads snapshots to show the running blocks and the variables.
class Session {
public:
    using Opener = std::function<Result<std::unique_ptr<serial::LineTransport>>(const std::string& port)>;
    using Flasher = std::function<Status(std::string& details)>;

    enum class State { Connecting, Flashing, Running, Finished, Stopped, Failed };

    struct Snapshot {
        State state = State::Connecting;
        std::string message;                    ///< For the status bar, in the user's language.
        std::string details;                    ///< Technical details of a failure.
        std::vector<std::string> runningBlocks;
        std::vector<std::string> problemBlocks; ///< Validation errors that prevent starting.
        std::map<std::string, double> variables;
        std::map<std::string, double> readings; ///< Sensor values read by the blocks, by pin label.
        std::vector<std::string> output;        ///< Last lines written to the serial monitor (at most kOutputLines)...
        std::size_t outputCount = 0;            ///< ...out of this many since the start.
    };
    static constexpr std::size_t kOutputLines = 200;

    Session(project::Project project, const Board& board, std::string port, Language language, Opener opener,
            Flasher flasher);
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;
    /// Stops the blocks and switches the outputs off.
    ~Session();

    void update(project::Project project);
    void stop();
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] bool active() const;

    /// Opens real serial ports.
    [[nodiscard]] static Opener serialOpener();
    /// Uploads the firmware with the bundled arduino-cli.
    [[nodiscard]] static Flasher arduinoCliFlasher(const Board& board, std::string port);

private:
    void run(project::Project project);
    void set(State state, std::string message, std::string details = {});

    const Board& board_;
    std::string port_;
    Language language_;
    Opener opener_;
    Flasher flasher_;
    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::optional<project::Project> pending_;
    std::atomic<bool> stopping_{false};
    std::thread thread_;
};

/// Source of the S2A live firmware (embedded in the application).
[[nodiscard]] std::string_view firmwareSource();

/// Compiles and uploads the S2A live firmware to `port`, writing it under `workDirectory`.
[[nodiscard]] Status uploadFirmware(const toolchain::ArduinoCli& cli, const Board& board, const std::string& port,
                                    const std::filesystem::path& workDirectory, std::string& details);

} // namespace s2a::live
