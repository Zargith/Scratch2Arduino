#include "s2a/boards.hpp"

#include "s2a/resources.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace s2a {

namespace {

using Json = nlohmann::json;

/// Thrown while parsing one board; turned into an Error by BoardDatabase::fromJson.
class BoardError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

std::optional<int> parseNumber(std::string_view text) {
    int value = 0;
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value);
    if (ec != std::errc{} || ptr != end || text.empty()) {
        return std::nullopt;
    }
    return value;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) ==
                      std::tolower(static_cast<unsigned char>(y));
           });
}

Pin& pinRef(Board& board, const std::string& reference, std::string_view field) {
    const Pin* pin = board.findPin(reference);
    if (pin == nullptr) {
        throw BoardError(std::string(field) + " refers to unknown pin '" + reference + "'");
    }
    return *std::find_if(board.pins.begin(), board.pins.end(),
                         [&](const Pin& candidate) { return candidate.number == pin->number; });
}

void addCapability(Board& board, const Json& references, PinCapability capability,
                   std::string_view field) {
    for (const auto& reference : references) {
        Pin& pin = pinRef(board, reference.get<std::string>(), field);
        if (!pin.has(PinCapability::Digital)) {
            throw BoardError(std::string(field) + ": pin '" + pin.label + "' is input-only");
        }
        pin.capabilities |= static_cast<std::uint8_t>(capability);
    }
}

std::vector<BusPin> parseBus(Board& board, const Json& node, std::string_view field) {
    std::vector<BusPin> bus;
    for (const auto& [role, reference] : node.items()) {
        bus.push_back({role, pinRef(board, reference.get<std::string>(), field).number});
    }
    return bus;
}

UsbId parseUsbId(const std::string& text) {
    // "2341:0043" (hexadecimal vendor:product)
    const auto colon = text.find(':');
    if (colon == std::string::npos) {
        throw BoardError("usb id '" + text + "' must look like VVVV:PPPP");
    }
    const auto hex = [&](std::string_view part) {
        unsigned value = 0;
        const char* end = part.data() + part.size();
        const auto [ptr, ec] = std::from_chars(part.data(), end, value, 16);
        if (ec != std::errc{} || ptr != end || part.size() != 4) {
            throw BoardError("usb id '" + text + "' must look like VVVV:PPPP");
        }
        return static_cast<std::uint16_t>(value);
    };
    const std::string_view view = text;
    return {hex(view.substr(0, colon)), hex(view.substr(colon + 1))};
}

Board parseBoard(const Json& json) {
    Board board;
    board.id = json.at("id").get<std::string>();
    if (board.id.empty() ||
        !std::all_of(board.id.begin(), board.id.end(), [](char c) {
            return (std::islower(static_cast<unsigned char>(c)) != 0) ||
                   (std::isdigit(static_cast<unsigned char>(c)) != 0) || c == '_' || c == '-';
        })) {
        throw BoardError("id '" + board.id + "' must use lowercase letters, digits, '-' or '_'");
    }
    board.name = json.at("name").get<std::string>();
    board.fqbn = json.at("fqbn").get<std::string>();
    if (std::count(board.fqbn.begin(), board.fqbn.end(), ':') < 2) {
        throw BoardError("fqbn '" + board.fqbn + "' must look like vendor:arch:board");
    }
    board.platform = json.at("platform").get<std::string>();
    {
        const auto first = board.fqbn.find(':');
        board.architecture = board.fqbn.substr(first + 1, board.fqbn.find(':', first + 1) - first - 1);
    }
    board.mcu = json.at("mcu").get<std::string>();
    board.voltage = json.at("voltage").get<double>();
    board.flashBytes = json.at("flashBytes").get<std::uint32_t>();
    board.ramBytes = json.at("ramBytes").get<std::uint32_t>();
    board.bluetooth = json.value("bluetooth", false);
    board.wifi = json.value("wifi", false);
    board.builtinLedActiveLow = json.value("builtinLedActiveLow", false);

    // Either a count (pins 0..n-1, as on Arduino boards), the list of usable GPIO numbers
    // (ESP32: numbers with gaps, the flash and boot pins left out), or the names printed on the
    // board with their GPIO number (NodeMCU: "D1" is GPIO 5).
    const Json& digital = json.at("digitalPins");
    std::vector<std::pair<int, std::string>> digitalPins;
    if (digital.is_array()) {
        for (const int number : digital.get<std::vector<int>>()) {
            digitalPins.emplace_back(number, std::to_string(number));
        }
    } else if (digital.is_object()) {
        for (const auto& [label, number] : digital.items()) {
            digitalPins.emplace_back(number.get<int>(), label);
        }
    } else {
        for (int number = 0; number < digital.get<int>(); ++number) {
            digitalPins.emplace_back(number, std::to_string(number));
        }
    }
    for (const auto& [number, label] : digitalPins) {
        board.pins.push_back({number, label, static_cast<std::uint8_t>(PinCapability::Digital)});
    }
    const Json& analog = json.at("analogPins");
    const auto inputOnly = analog.value("inputOnly", std::vector<std::string>{});
    if (analog.contains("count")) {
        // Arduino boards: A0..An-1 numbered from firstNumber.
        const int analogCount = analog.at("count").get<int>();
        const int firstNumber = analog.at("firstNumber").get<int>();
        for (int index = 0; index < analogCount; ++index) {
            const std::string label = "A" + std::to_string(index);
            const bool digitalToo = std::find(inputOnly.begin(), inputOnly.end(), label) == inputOnly.end();
            std::uint8_t capabilities = static_cast<std::uint8_t>(PinCapability::Analog);
            if (digitalToo) {
                capabilities |= static_cast<std::uint8_t>(PinCapability::Digital);
            }
            board.pins.push_back({firstNumber + index, label, capabilities});
        }
    } else {
        // GPIO boards: "pins" are digital pins that also read analog values, "inputOnly" are
        // analog inputs that cannot be written (their label is their number).
        for (const std::string& label : inputOnly) {
            const auto number = parseNumber(label);
            if (!number) {
                throw BoardError("analogPins.inputOnly: '" + label + "' is not a pin number");
            }
            board.pins.push_back({*number, label, static_cast<std::uint8_t>(PinCapability::Analog)});
        }
    }
    std::sort(board.pins.begin(), board.pins.end(),
              [](const Pin& a, const Pin& b) { return a.number < b.number; });
    for (std::size_t i = 1; i < board.pins.size(); ++i) {
        if (board.pins[i].number == board.pins[i - 1].number) {
            throw BoardError("pin number " + std::to_string(board.pins[i].number) + " is used twice");
        }
    }

    if (analog.contains("pins")) {
        for (const auto& reference : analog.at("pins")) {
            pinRef(board, reference.get<std::string>(), "analogPins.pins").capabilities |=
                static_cast<std::uint8_t>(PinCapability::Analog);
        }
    }
    addCapability(board, json.at("pwmPins"), PinCapability::Pwm, "pwmPins");
    addCapability(board, json.at("interruptPins"), PinCapability::Interrupt, "interruptPins");
    board.builtinLed = pinRef(board, json.at("builtinLed").get<std::string>(), "builtinLed").number;
    if (json.contains("i2c")) {
        board.i2c = parseBus(board, json.at("i2c"), "i2c");
    }
    if (json.contains("spi")) {
        board.spi = parseBus(board, json.at("spi"), "spi");
    }
    for (const auto& usb : json.value("usb", std::vector<std::string>{})) {
        board.usbIds.push_back(parseUsbId(usb));
    }
    return board;
}

} // namespace

const Pin* Board::findPin(int number) const {
    const auto it = std::lower_bound(pins.begin(), pins.end(), number,
                                     [](const Pin& pin, int value) { return pin.number < value; });
    return it != pins.end() && it->number == number ? &*it : nullptr;
}

const Pin* Board::findPin(std::string_view labelOrNumber) const {
    if (const auto number = parseNumber(labelOrNumber)) {
        return findPin(*number);
    }
    if (labelOrNumber.size() > 1 && (labelOrNumber[0] == 'D' || labelOrNumber[0] == 'd')) {
        if (const auto number = parseNumber(labelOrNumber.substr(1))) {
            const Pin* pin = findPin(*number);
            if (pin != nullptr && pin->label == labelOrNumber.substr(1)) {
                return pin; // "D13" on an Arduino board
            }
        }
    }
    const auto it = std::find_if(pins.begin(), pins.end(), [&](const Pin& pin) {
        return equalsIgnoreCase(pin.label, labelOrNumber);
    });
    return it != pins.end() ? &*it : nullptr;
}

std::vector<const Pin*> Board::pinsWith(PinCapability capability) const {
    std::vector<const Pin*> result;
    for (const Pin& pin : pins) {
        if (pin.has(capability)) {
            result.push_back(&pin);
        }
    }
    return result;
}

Result<BoardDatabase> BoardDatabase::fromJson(const std::vector<std::string_view>& documents) {
    BoardDatabase database;
    for (std::size_t index = 0; index < documents.size(); ++index) {
        const Json json = Json::parse(documents[index], nullptr, /*allow_exceptions=*/false);
        const std::string where = "board #" + std::to_string(index);
        if (json.is_discarded() || !json.is_object()) {
            return Error{ErrorCode::InvalidJson, where + " is not a JSON object"};
        }
        try {
            database.boards_.push_back(parseBoard(json));
        } catch (const BoardError& error) {
            return Error{ErrorCode::InvalidFormat, where + ": " + error.what()};
        } catch (const Json::exception& error) {
            return Error{ErrorCode::InvalidFormat, where + ": " + error.what()};
        }
    }
    std::sort(database.boards_.begin(), database.boards_.end(),
              [](const Board& a, const Board& b) { return a.id < b.id; });
    for (std::size_t i = 1; i < database.boards_.size(); ++i) {
        if (database.boards_[i].id == database.boards_[i - 1].id) {
            return Error{ErrorCode::InvalidFormat, "board id '" + database.boards_[i].id + "' is used twice"};
        }
    }
    return database;
}

const BoardDatabase& BoardDatabase::builtin() {
    static const BoardDatabase database = [] {
        std::vector<std::string_view> documents;
        for (const auto& resource : resources::list("resources/boards/")) {
            documents.push_back(resource.data);
        }
        auto result = fromJson(documents);
        // The embedded files are validated by the unit tests; never crash the application.
        return result.ok() ? std::move(result).value() : BoardDatabase{};
    }();
    return database;
}

const Board* BoardDatabase::find(std::string_view id) const {
    const auto it = std::lower_bound(boards_.begin(), boards_.end(), id,
                                     [](const Board& board, std::string_view value) { return board.id < value; });
    return it != boards_.end() && it->id == id ? &*it : nullptr;
}

std::vector<const Board*> BoardDatabase::matchUsb(UsbId id) const {
    std::vector<const Board*> matches;
    for (const Board& board : boards_) {
        const bool found = std::any_of(board.usbIds.begin(), board.usbIds.end(), [&](const UsbId& usb) {
            return usb.vendor == id.vendor && usb.product == id.product;
        });
        if (found) {
            matches.push_back(&board);
        }
    }
    return matches;
}

} // namespace s2a
