#pragma once

#include "s2a/result.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace s2a {

/// What a pin can do. A pin usually has several capabilities.
enum class PinCapability : std::uint8_t {
    Digital = 1U << 0U,   ///< digitalRead / digitalWrite
    Analog = 1U << 1U,    ///< analogRead
    Pwm = 1U << 2U,       ///< analogWrite
    Interrupt = 1U << 3U, ///< attachInterrupt
};

struct Pin {
    int number = 0;          ///< Number used in Arduino code (A0 is 14 on an Uno).
    std::string label;       ///< Name printed on the board: "13", "A0".
    std::uint8_t capabilities = 0;

    [[nodiscard]] bool has(PinCapability capability) const noexcept {
        return (capabilities & static_cast<std::uint8_t>(capability)) != 0U;
    }
};

struct UsbId {
    std::uint16_t vendor = 0;
    std::uint16_t product = 0;
};

/// Pins of a bus, by role (I2C: sda, scl; SPI: mosi, miso, sck, ss).
struct BusPin {
    std::string role;
    int number = 0;
};

/// A row of header pins on the drawing of a board (wiring diagram).
struct BoardHeader {
    enum class Side { Top, Bottom, Right }; ///< Where the wires leave the board.
    struct Entry {
        std::string label; ///< As printed on the board ("TX 1", "D5").
        std::string pin;   ///< Label of the pin in the database ("1", "D5"), a power pin ("5V", "GND"),
                           ///< or "-" for a pin the blocks cannot use (serial, flash).
    };
    Side side = Side::Top;
    double x = 0;     ///< First pin, from the top-left corner of the board.
    double y = 0;
    double pitch = 0; ///< Distance between two pins (along x, or along y on the right side).
    std::vector<std::optional<Entry>> pins; ///< nullopt: a gap between two header blocks.
};

/// How a board is drawn on wiring diagrams: a coloured rectangle and its headers.
struct BoardDrawing {
    double width = 0; ///< 0: no drawing.
    double height = 0;
    std::string colour;
    std::string title;
    std::vector<BoardHeader> headers;
};

/// Description of a supported board (resources/boards/<id>.json).
struct Board {
    std::string id;       ///< Stable identifier stored in projects: "uno".
    std::string name;     ///< Display name: "Arduino Uno".
    std::string fqbn;     ///< arduino-cli fully qualified board name.
    std::string platform; ///< arduino-cli core to install: "arduino:avr".
    std::string architecture; ///< Second part of the fqbn: "avr", "esp32".
    std::string mcu;
    double voltage = 5.0;
    std::uint32_t flashBytes = 0;
    std::uint32_t ramBytes = 0;
    int builtinLed = -1;
    bool bluetooth = false; ///< Classic Bluetooth (ESP32): the phone blocks work.
    bool wifi = false;      ///< Wi-Fi (ESP32, ESP8266): Live mode over Wi-Fi.
    /// An HC-05 / HC-06 Bluetooth module can be wired to it for the Live mode (Arduino boards).
    bool bluetoothModule = false;
    bool builtinLedActiveLow = false; ///< The on-board LED lights when its pin is LOW (ESP8266).
    BoardDrawing drawing;
    std::vector<Pin> pins; ///< Sorted by number.
    std::vector<BusPin> i2c;
    std::vector<BusPin> spi;
    /// USB identifiers seen when the board is plugged in. Generic USB-serial chips (CH340, FTDI)
    /// are shared by many boards, so a match is only a suggestion.
    std::vector<UsbId> usbIds;

    /// Pin by label or number: "13", "D13", "A0" or "14" (A0 on an Uno).
    [[nodiscard]] const Pin* findPin(std::string_view labelOrNumber) const;
    [[nodiscard]] const Pin* findPin(int number) const;
    [[nodiscard]] std::vector<const Pin*> pinsWith(PinCapability capability) const;
};

/// The boards Scratch2Arduino knows about.
class BoardDatabase {
public:
    /// Parses and checks board descriptions (one JSON document per board).
    [[nodiscard]] static Result<BoardDatabase> fromJson(const std::vector<std::string_view>& documents);

    /// Boards embedded in the application (resources/boards/*.json).
    [[nodiscard]] static const BoardDatabase& builtin();

    [[nodiscard]] const std::vector<Board>& boards() const noexcept { return boards_; }
    [[nodiscard]] const Board* find(std::string_view id) const;
    /// Boards that may match a plugged-in USB device.
    [[nodiscard]] std::vector<const Board*> matchUsb(UsbId id) const;

    /// Board selected for new projects.
    static constexpr std::string_view kDefaultBoardId = "uno";

private:
    std::vector<Board> boards_; ///< Sorted by id.
};

} // namespace s2a
