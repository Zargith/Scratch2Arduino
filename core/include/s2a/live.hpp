#pragma once

#include "s2a/arduino_cli.hpp"
#include "s2a/blocks.hpp"
#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/project.hpp"
#include "s2a/result.hpp"
#include "s2a/serial.hpp"

#include <array>
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
namespace s2a::script {
struct Host;
struct ScriptError;
} // namespace s2a::script

namespace s2a::live {

inline constexpr int kBaud = 115200;
inline constexpr std::string_view kHello = "S2A 9"; ///< Changes with the protocol: an older firmware is uploaded again.

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
    /// Temperature (°C) and humidity (%) of a DHT11 (model 11) or DHT22 (22); nullopt when it does not answer.
    [[nodiscard]] virtual std::optional<std::pair<double, double>> dht(int pin, int model) = 0;
    /// Text on line `row` (1-2) of the I2C LCD; row 0 clears it.
    virtual void lcdLine(int row, const std::string& text) = 0;
    /// Text on line `row` (1-2) of a 16-pin LCD wired to `pins` (RS, E, D4, D5, D6, D7); row 0 clears it.
    virtual void lcd16Line(const std::array<int, 6>& pins, int row, const std::string& text) = 0;
    /// Text on line `row` (1-8) of the I2C OLED screen; row 0 clears it.
    virtual void oledLine(int row, const std::string& text) = 0;
    /// Colour of LED `index` (1-count, 0: all) of a NeoPixel strip.
    virtual void pixels(int pin, int count, int index, int red, int green, int blue) = 0;
    /// Row `row` (1-8) of a MAX7219 LED matrix, left column = high bit.
    virtual void matrixRow(int din, int clk, int cs, int row, int bits) = 0;
    /// Every output low, no sound, servos released, strip, matrix and screens cleared (Live mode stopped).
    virtual void reset() = 0;
    /// Last line sent by the phone connected to the board over Bluetooth, once; nullopt when none.
    [[nodiscard]] virtual std::optional<std::string> phoneMessage() { return std::nullopt; }
    /// Sends a line to the phone connected to the board over Bluetooth.
    virtual void phoneSend(const std::string& text) { (void)text; }
    /// The phone talks through an HC-05 / HC-06 module on these pins (Arduino boards).
    virtual void phoneModule(int rx, int tx) {
        (void)rx;
        (void)tx;
    }
};

/// The user's own code ("My code", Python or JavaScript) as the blocks use it in Live mode.
class UserCode {
public:
    UserCode() = default;
    UserCode(const UserCode&) = delete;
    UserCode& operator=(const UserCode&) = delete;
    UserCode(UserCode&&) = delete;
    UserCode& operator=(UserCode&&) = delete;
    virtual ~UserCode() = default;

    /// Runs a function with number arguments; its result as a number (0 when it gives none).
    virtual double call(const std::string& name, const std::vector<double>& arguments) = 0;
    [[nodiscard]] virtual double get(const std::string& name) = 0;
    virtual void set(const std::string& name, double value) = 0;
    /// True once the code failed (the Live mode stops and shows why).
    [[nodiscard]] virtual bool failed() const = 0;
};

/// `text` in lower case, without the accents of Latin letters nor spaces around: " Éteins" is
/// "eteins" (how the phone blocks compare messages, as the generated code does).
[[nodiscard]] std::string simpleText(std::string_view text);

/// How an HC-05 / HC-06 Bluetooth module is wired to an Arduino board running the Live firmware.
struct BluetoothModule {
    int rx = 10;     ///< Board pin that receives: wired to TXD of the module.
    int tx = 11;     ///< Board pin that sends: wired to RXD of the module (through a divider).
    int baud = 9600; ///< Speed of the module (9600 for a new HC-05 or HC-06).

    friend bool operator==(const BluetoothModule& a, const BluetoothModule& b) {
        return a.rx == b.rx && a.tx == b.tx && a.baud == b.baud;
    }
};

/// Why `module` cannot be used on `board` (a key of the "module" i18n section), or "" when it can:
/// "no_module" (the board takes no module), "pins_usb" (0 and 1 are the USB link), "pins_same",
/// "pin_missing", "pin_receive" (on a Mega, a pin that cannot receive with SoftwareSerial), "speed".
[[nodiscard]] std::string bluetoothModuleProblem(const Board& board, const BluetoothModule& module);

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
    [[nodiscard]] std::optional<std::pair<double, double>> dht(int pin, int model) override;
    void lcdLine(int row, const std::string& text) override;
    void lcd16Line(const std::array<int, 6>& pins, int row, const std::string& text) override;
    void oledLine(int row, const std::string& text) override;
    void pixels(int pin, int count, int index, int red, int green, int blue) override;
    void matrixRow(int din, int clk, int cs, int row, int bits) override;
    void reset() override;
    [[nodiscard]] std::optional<std::string> phoneMessage() override;
    void phoneSend(const std::string& text) override;
    void phoneModule(int rx, int tx) override;

    /// First communication error since the last call (the board was unplugged...), if any.
    [[nodiscard]] std::optional<Error> takeError();

    /// Wi-Fi state of an ESP32: its own network and its address on the joined one ("" if none).
    struct NetworkState {
        std::string accessPoint;        ///< "S2A-1A2B"
        std::string accessPointAddress; ///< "192.168.4.1"
        std::string address;            ///< On the home network, "" while not joined.
    };
    [[nodiscard]] Result<NetworkState> networkState();
    /// Saves the home network on the board and joins it (an empty name forgets it).
    [[nodiscard]] Status joinNetwork(const std::string& name, const std::string& password);

    /// HC-05 / HC-06 Bluetooth module of an Arduino board (see BluetoothModule).
    /// The settings saved on the board, nullopt when it has none (or its firmware has no module).
    [[nodiscard]] Result<std::optional<BluetoothModule>> bluetoothModule();
    /// Saves the module's pins and speed on the board (nullopt forgets them). The firmware starts
    /// the link at once, or at its next start when another link already runs.
    [[nodiscard]] Status setBluetoothModule(const std::optional<BluetoothModule>& module);

private:
    /// True when the firmware answers a "?" now (false while the board restarts).
    [[nodiscard]] Result<bool> confirm();
    /// Sends `command` and returns the reply line ("ok", "r 2 1"...), or nullopt on error.
    std::optional<std::string> request(const std::string& command,
                                       std::chrono::milliseconds timeout = std::chrono::milliseconds(500));

    serial::LineTransport& transport_;
    std::optional<Error> error_;
};

/// Runs the scripts of a project, Scratch style: every script started by a hat runs in turn,
/// a script gives way at each wait and at the end of each loop turn.
class Interpreter {
public:
    using Clock = std::chrono::milliseconds; ///< Time since the start of the Live mode.

    /// `userCode`: the project's Python or JavaScript code, used by the "My code" blocks (may be null).
    Interpreter(project::Project project, const BlockCatalog& catalog, const Board& board, Pins& pins,
                UserCode* userCode = nullptr);

    /// Runs every script until it waits; call it often (every 10 ms or so).
    void step(Clock now);

    /// Takes a new version of the blocks: running scripts continue where they are when their
    /// current block still exists, variables keep their values.
    void update(project::Project project);

    [[nodiscard]] bool finished() const;

    /// Step-by-step mode. Paused, no block runs: each script shows the block it runs next, and
    /// stepOnce() runs one block (of the scripts in turn) at the next step(). Waits are skipped
    /// while paused, so that "Next block" never seems to do nothing.
    void setPaused(bool paused) noexcept { paused_ = paused; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    void stepOnce() noexcept { ++pendingSteps_; }

    /// Block being run by each running script (to highlight it); paused: the block it runs next.
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
    /// Runs at most `limit` blocks of the thread (0: only shows the block it runs next).
    void run(Thread& thread, Clock now, int limit);
    double number(const project::Block& block, const std::string& name);
    bool condition(const project::Block& block, const std::string& name);
    double evaluate(const project::Block& block);
    int pin(const project::Block& block, const std::string& name, PinMode mode);
    /// Text slot: typed text or the value of a reporter, as the serial monitor shows it.
    [[nodiscard]] std::string text(const project::Block& block, const std::string& name);
    /// Pin number of a pin menu, without changing its mode (-1 when invalid).
    [[nodiscard]] int pinNumber(const project::Block& block, const std::string& name) const;
    void record(int pin, double value);
    /// Takes the phone's new message, if any (phone blocks).
    void pollPhone();
    [[nodiscard]] std::string field(const project::Block& block, const std::string& name) const;
    bool restore(Thread& thread, const std::vector<Frame>& old);

    project::Project project_;
    const BlockCatalog& catalog_;
    const Board& board_;
    Pins& pins_;
    std::vector<Thread> threads_;
    bool paused_ = false;
    int pendingSteps_ = 0;
    std::size_t stepCursor_ = 0; ///< Next thread to run one block in step-by-step mode.
    std::map<std::string, double> variables_;
    std::map<int, PinMode> modes_;
    std::map<std::string, double> readings_;
    std::map<int, std::pair<double, double>> dht_;        ///< Last good DHT values by pin.
    std::map<int, int> stripLengths_;                     ///< NeoPixel pin -> number of LEDs.
    std::map<std::string, std::array<int, 8>> matrices_;  ///< "din clk cs" -> rows shown.
    std::vector<std::string> output_;
    UserCode* userCode_ = nullptr;
    /// Number of parameters of each function of the user code (the "My code" blocks).
    std::map<std::string, std::size_t> userParameters_;
    void readUserDeclarations();
    std::vector<double> userArguments(const project::Block& block);
    std::string phoneLine_;   ///< Last message of the phone, until a block recognises it.
    double phoneNumber_ = 0;  ///< Last number sent by the phone.
    std::mt19937 random_{std::random_device{}()};
};

/// Live mode as the editors use it: connects to the board (uploading the firmware when it does
/// not answer), then runs the interpreter (or the Python/JavaScript program of a text project,
/// see script.hpp) on a background thread. The editor sends new versions of the blocks and
/// reads snapshots to show the running blocks and the variables.
class Session {
public:
    using Opener = std::function<Result<std::unique_ptr<serial::LineTransport>>(const std::string& port)>;
    /// Uploads the Live firmware; empty when it cannot be done (Wi-Fi: it needs the USB cable).
    using Flasher = std::function<Status(std::string& details)>;

    enum class State { Connecting, Flashing, Running, Finished, Stopped, Failed };

    /// A line of the program's output: printed (standard output) or an error (error output).
    struct OutputLine {
        std::string text;
        bool error = false;
    };

    struct Snapshot {
        State state = State::Connecting;
        std::string message;                    ///< For the status bar, in the user's language.
        std::string details;                    ///< Technical details of a failure.
        std::vector<std::string> runningBlocks;
        std::vector<std::string> problemBlocks; ///< Validation errors that prevent starting.
        std::map<std::string, double> variables;
        std::map<std::string, double> readings; ///< Sensor values read by the blocks, by pin label.
        std::vector<OutputLine> output;         ///< Last lines written by the program (at most kOutputLines)...
        std::size_t outputCount = 0;            ///< ...out of this many since the start.
        bool paused = false;                    ///< Step-by-step mode: blocks wait for "Next block".
        int errorLine = 0;                      ///< Python/JavaScript: line of the error that stopped the program...
        std::string errorFile;                  ///< ...in the program ("") or in "My code" ("user").
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
    /// Step-by-step mode (block programs; a Python or JavaScript program ignores it).
    void pause(bool paused);
    /// Paused: runs one more block.
    void stepOnce();
    [[nodiscard]] Snapshot snapshot() const;
    [[nodiscard]] bool active() const;

    /// Opens real serial ports.
    [[nodiscard]] static Opener serialOpener();
    /// Opens serial ports, and Wi-Fi connections for "wifi:<address>" ports.
    [[nodiscard]] static Opener opener();
    /// Uploads the firmware with the bundled arduino-cli.
    [[nodiscard]] static Flasher arduinoCliFlasher(const Board& board, std::string port);

private:
    void run(project::Project project);
    /// Python and JavaScript projects: runs the program on this thread until it ends or Stop.
    void runScript(const project::Project& project, LiveLink& link);
    /// What a Python/JavaScript program uses: the board's pins, the Live panel, Stop.
    script::Host scriptHost(LiveLink& link, std::optional<Error>& linkError);
    /// Failed with the error of a program ("") or of "My code" ("user").
    void failScript(const script::ScriptError& error, const std::string& file);
    void set(State state, std::string message, std::string details = {});
    /// Adds a line to the output (mutex_ held), keeping the last kOutputLines.
    void addOutput(std::string text, bool error);

    const Board& board_;
    std::string port_;
    Language language_;
    Opener opener_;
    Flasher flasher_;
    mutable std::mutex mutex_;
    Snapshot snapshot_;
    std::optional<project::Project> pending_;
    std::atomic<bool> stopping_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int> steps_{0}; ///< "Next block" clicks not yet given to the interpreter.
    std::thread thread_;
};

/// Source of the S2A live firmware (embedded in the application).
[[nodiscard]] std::string_view firmwareSource();

/// Compiles and uploads the S2A live firmware to `port`, writing it under `workDirectory`.
[[nodiscard]] Status uploadFirmware(const toolchain::ArduinoCli& cli, const Board& board, const std::string& port,
                                    const std::filesystem::path& workDirectory, std::string& details);

} // namespace s2a::live
