#pragma once

#include "s2a/boards.hpp"
#include "s2a/result.hpp"
#include "s2a/serial.hpp"

#include <array>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Simulation: a virtual board on the screen, without any hardware. It answers the commands of
/// the Live mode like the S2A live firmware (firmware/s2a_live), so the blocks, the step-by-step
/// mode and the Python / JavaScript programs run on it exactly as on a real board. The editor
/// shows its outputs (LEDs, screens, servos, sounds...) and changes its inputs (buttons,
/// potentiometers, sensors).
namespace s2a::sim {

/// Prefix of the "port" of the virtual board: "sim:uno".
inline constexpr std::string_view kPortPrefix = "sim:";

/// Whether `port` is the virtual board (nothing can be uploaded to it, it has no serial monitor).
[[nodiscard]] bool isSimulated(std::string_view port) noexcept;

/// Value of an analog input nobody moved yet (a potentiometer in the middle).
inline constexpr int kDefaultAnalog = 512;
/// Distance seen by an ultrasonic sensor nobody moved yet (cm).
inline constexpr double kDefaultDistance = 100;
/// Temperature (°C) and humidity (%) of a DHT sensor nobody changed yet.
inline constexpr double kDefaultTemperature = 21;
inline constexpr double kDefaultHumidity = 50;
/// Lines sent to the phone kept for the editor.
inline constexpr std::size_t kPhoneLines = 50;

class VirtualBoard {
public:
    using Clock = std::chrono::steady_clock;

    /// What the editor draws. Pins are named by their label ("13", "A0", "D5").
    struct State {
        struct Output {
            int level = 0;    ///< 0-255: 0 or 255 for a digital pin, the duty cycle for PWM.
            bool pwm = false; ///< Set by "analogWrite".
        };
        std::map<std::string, std::string> modes; ///< label -> "input", "pullup" or "output".
        std::map<std::string, Output> outputs;    ///< Pins set as outputs.
        std::map<std::string, int> servos;        ///< label -> angle (0-180).
        std::map<std::string, double> steppers;   ///< IN1 pin label -> angle turned since the start (degrees).
        std::map<std::string, std::string> digits; ///< TM1637 CLK pin label -> its 4 characters ("  42", "----"), "" when off.
        std::map<std::string, int> tones;         ///< label -> frequency now sounding (Hz).
        std::array<std::string, 2> lcd;           ///< I2C 16x2 screen.
        std::array<std::string, 2> lcd16;         ///< 16-pin 16x2 screen.
        std::array<std::string, 8> oled;          ///< I2C OLED screen (8 lines of 16 characters).
        std::map<std::string, std::vector<std::array<int, 3>>> pixels; ///< NeoPixel pin -> colours.
        std::map<std::string, std::array<int, 8>> matrices;            ///< MAX7219 DIN pin -> rows (left = high bit).
        std::vector<std::string> phone;           ///< Lines the program sent to the phone (last kPhoneLines).
        // Inputs, as the editor set them.
        std::map<std::string, bool> pressed;      ///< label -> button pressed.
        std::map<std::string, int> analog;        ///< label -> 0-1023.
        std::map<std::string, double> distances;  ///< Trig pin label -> cm.
        std::map<std::string, std::pair<double, double>> climates; ///< DHT pin label -> (°C, %).
    };

    explicit VirtualBoard(const Board& board);

    /// The firmware's answer to one command line ("ok", "r 2 1", "! Z" for an unknown one).
    [[nodiscard]] std::string answer(std::string_view command);

    [[nodiscard]] State state() const;
    [[nodiscard]] const Board& board() const noexcept { return board_; }

    // Inputs, by pin label or number (an unknown pin is ignored).
    /// A button between the pin and GND: pressed, the pin reads LOW with the pull-up (HIGH without).
    void press(std::string_view pin, bool pressed);
    void setAnalog(std::string_view pin, int value);
    void setDistance(std::string_view trigPin, double centimetres);
    void setClimate(std::string_view pin, double temperature, double humidity);
    /// A line typed on the virtual phone (read once by the phone blocks).
    void phoneSays(std::string text);

private:
    [[nodiscard]] std::string label(int number) const;
    [[nodiscard]] std::optional<int> number(std::string_view label) const;
    void reset();

    const Board& board_;
    mutable std::mutex mutex_;
    std::map<int, char> modes_; ///< 'I', 'U' or 'O'.
    std::map<int, State::Output> outputs_;
    std::map<int, int> servos_;
    std::map<int, long> steppers_; ///< IN1 pin -> steps turned.
    std::map<int, std::string> digits_; ///< TM1637 CLK pin -> what it shows.
    int tonePin_ = -1;
    int toneFrequency_ = 0;
    std::optional<Clock::time_point> toneEnd_;
    std::array<std::string, 2> lcd_;
    std::array<std::string, 2> lcd16_;
    std::array<std::string, 8> oled_;
    std::map<int, std::vector<std::array<int, 3>>> pixels_;
    std::map<int, std::array<int, 8>> matrices_;
    std::deque<std::string> phone_;
    std::string phoneMessage_;
    std::map<int, bool> pressed_;
    std::map<int, int> analog_;
    std::map<int, double> distances_;
    std::map<int, std::pair<double, double>> climates_;
};

/// The Live mode's connection to a virtual board: it greets at once like a freshly started firmware.
class Connection final : public serial::LineTransport {
public:
    explicit Connection(std::shared_ptr<VirtualBoard> board);

    [[nodiscard]] Status writeLine(std::string_view line) override;
    [[nodiscard]] Result<std::optional<std::string>> readLine(std::chrono::milliseconds timeout) override;

private:
    std::shared_ptr<VirtualBoard> board_;
    std::deque<std::string> pending_;
};

} // namespace s2a::sim
