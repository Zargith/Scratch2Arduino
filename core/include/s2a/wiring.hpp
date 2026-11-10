#pragma once

#include "s2a/blocks.hpp"
#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/project.hpp"

#include <string>
#include <vector>

/// What must be wired to the board for a project, computed from its blocks (Phase 13): the
/// components, each connection to a board pin, and the problems of the wiring. The parts are
/// described in resources/components.json; each block names its part in the catalog.
namespace s2a::wiring {

struct Connection {
    std::string componentPin; ///< "+ leg (long)", "Trig"... (in the chosen language)
    std::string boardPin;     ///< Pin label ("13", "A0", "D5") or power: "5V", "3V3", "VIN", "GND".
    std::string via;          ///< Part in between ("220 Ω resistor"), "" for a plain wire.
};

struct Component {
    std::string kind; ///< Key in resources/components.json: "button", "servo"...
    std::string name; ///< In the chosen language.
    std::vector<Connection> connections;
    std::string note;                  ///< Advice in the chosen language, "" for none.
    int current = 0;                   ///< Typical draw (mA).
    std::vector<std::string> blockIds; ///< Blocks using this component.
};

enum class WarningKind {
    PinUsedTwice, ///< Two components on the same pin.
    I2cPinTaken,  ///< A screen uses I2C, and its SDA/SCL pin also drives another component.
    PowerBudget,  ///< More current than a USB port gives (500 mA).
};

struct Warning {
    WarningKind kind = WarningKind::PinUsedTwice;
    std::string message; ///< In the chosen language.
    std::vector<std::string> blockIds;
};

struct Plan {
    std::vector<Component> components; ///< In the order of the blocks.
    std::vector<Warning> warnings;
};

/// USB power available for the components.
inline constexpr int kUsbBudget = 500;

/// The wiring of the scripts that run (loose blocks are ignored, like in the program).
[[nodiscard]] Plan plan(const project::Project& project, const BlockCatalog& catalog, const Board& board,
                        Language language);

/// Same, with the built-in catalog and the project's board (an empty plan for an unknown board).
[[nodiscard]] Plan plan(const project::Project& project, Language language);

/// The wiring diagram of `plan` as an SVG picture: the board with its pins, the components as
/// numbered cards (in the order of the plan), power rails (+ and GND) and one wire per
/// connection, with its resistor. `title` is written at the top.
[[nodiscard]] std::string svg(const Plan& plan, const Board& board, const std::string& title);

/// Kinds described in resources/components.json.
[[nodiscard]] std::vector<std::string> componentKinds();

/// Block pin arguments a component kind connects ("PIN", "TRIG_PIN"...).
[[nodiscard]] std::vector<std::string> blockPins(const std::string& kind);

} // namespace s2a::wiring
