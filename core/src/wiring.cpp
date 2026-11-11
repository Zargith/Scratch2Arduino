#include "s2a/wiring.hpp"

#include "s2a/resources.hpp"

#include <map>
#include <nlohmann/json.hpp>
#include <set>

namespace s2a::wiring {

namespace {

using Json = nlohmann::json;
using project::Block;
using project::Stack;

/// resources/components.json, parsed once.
const Json& components() {
    static const Json parsed = [] {
        const auto data = resources::find("resources/components.json");
        Json json = data ? Json::parse(*data, nullptr, /*allow_exceptions=*/false) : Json();
        return json.is_object() && json.contains("components") ? json.at("components") : Json::object();
    }();
    return parsed;
}

std::string localized(const Json& node, const char* key, Language language) {
    if (!node.contains(key) || !node.at(key).is_object()) {
        return {};
    }
    return node.at(key).value(language == Language::English ? "en" : "fr", std::string{});
}

bool isPower(const std::string& target) {
    return target == "VCC" || target == "5V" || target == "GND" || target == "SDA" || target == "SCL";
}

class Planner {
public:
    Planner(const BlockCatalog& catalog, const Board& board, Language language)
        : catalog_(catalog), board_(board), language_(language) {}

    void stack(const Stack& blocks) {
        for (const Block& block : blocks) {
            visit(block);
        }
    }

    Plan finish() {
        warnPins();
        warnPower();
        return std::move(plan_);
    }

private:
    void visit(const Block& block) {
        if (const BlockDefinition* definition = catalog_.find(block.type);
            definition != nullptr && !definition->component.empty()) {
            add(block, *definition);
        }
        for (const auto& [name, input] : block.inputs) {
            if (input.block) {
                visit(*input.block);
            }
        }
        for (const auto& [name, inner] : block.statements) {
            stack(inner);
        }
    }

    /// The label of the pin chosen in `arg` of `block` ("13", "A0", "D5"), as written when unknown.
    std::string blockPin(const Block& block, const BlockDefinition& definition, const std::string& arg) const {
        std::string value;
        if (const auto field = block.fields.find(arg); field != block.fields.end() && !field->second.empty()) {
            value = field->second;
        } else if (const BlockArg* found = definition.findArg(arg)) {
            value = found->defaultValue;
        }
        const Pin* pin = board_.findPin(value);
        return pin != nullptr ? pin->label : value;
    }

    std::string busPin(const char* role) const {
        for (const BusPin& bus : board_.i2c) {
            if (bus.role == role) {
                if (const Pin* pin = board_.findPin(bus.number)) {
                    return pin->label;
                }
            }
        }
        return role[1] == 'd' ? "SDA" : "SCL";
    }

    std::string target(const std::string& to, const Block& block, const BlockDefinition& definition) const {
        const bool fiveVolts = board_.voltage >= 4.5;
        if (to == "VCC") {
            return fiveVolts ? "5V" : "3V3";
        }
        if (to == "5V") {
            return fiveVolts ? "5V" : "VIN"; // the USB 5 V on 3.3 V boards
        }
        if (to == "GND") {
            return "GND";
        }
        if (to == "SDA") {
            return busPin("sda");
        }
        if (to == "SCL") {
            return busPin("scl");
        }
        return blockPin(block, definition, to);
    }

    void add(const Block& block, const BlockDefinition& definition) {
        const Json& all = components();
        if (!all.contains(definition.component)) {
            return; // checked by the tests: every catalog component is described
        }
        const Json& description = all.at(definition.component);
        Component component;
        component.kind = definition.component;
        component.name = localized(description, "name", language_);
        component.current = description.value("current", 0);
        component.note = localized(description, "note", language_);
        if (board_.voltage < 4.5) {
            const std::string extra = localized(description, "note3v3", language_);
            component.note += (component.note.empty() || extra.empty() ? "" : " ") + extra;
        }
        std::string key = component.kind;
        for (const Json& connection : description.value("connections", Json::array())) {
            const std::string to = connection.value("to", std::string{});
            Connection wire{localized(connection, "pin", language_), target(to, block, definition),
                            localized(connection, "via", language_), connection.value("short", std::string{})};
            if (wire.shortPin.empty()) {
                wire.shortPin = wire.componentPin;
            }
            if (!isPower(to)) {
                key += "|" + wire.boardPin;
                signals_[wire.boardPin].insert(key);
            } else if (to == "SDA" || to == "SCL") {
                i2c_[wire.boardPin] = key;
            }
            component.connections.push_back(std::move(wire));
        }
        if (const auto found = index_.find(key); found != index_.end()) {
            plan_.components[found->second].blockIds.push_back(block.id);
            return;
        }
        component.blockIds.push_back(block.id);
        index_[key] = plan_.components.size();
        plan_.components.push_back(std::move(component));
    }

    const Component& byKey(const std::string& key) const { return plan_.components.at(index_.at(key)); }

    void warnPins() {
        for (const auto& [pin, keys] : signals_) {
            std::vector<std::string> users(keys.begin(), keys.end());
            if (const auto bus = i2c_.find(pin); bus != i2c_.end()) {
                for (const std::string& other : users) {
                    const Component& screen = byKey(bus->second);
                    Warning warning{WarningKind::I2cPinTaken,
                                    tr("wiring.i2c_taken", language_,
                                       {{"pin", pin}, {"screen", screen.name}, {"other", byKey(other).name}}),
                                    byKey(other).blockIds};
                    plan_.warnings.push_back(std::move(warning));
                }
            }
            if (users.size() < 2) {
                continue;
            }
            Warning warning{WarningKind::PinUsedTwice,
                            tr("wiring.pin_twice", language_,
                               {{"pin", pin}, {"first", byKey(users[0]).name}, {"second", byKey(users[1]).name}}),
                            {}};
            for (const std::string& user : users) {
                const auto& ids = byKey(user).blockIds;
                warning.blockIds.insert(warning.blockIds.end(), ids.begin(), ids.end());
            }
            plan_.warnings.push_back(std::move(warning));
        }
    }

    void warnPower() {
        int total = 0;
        for (const Component& component : plan_.components) {
            total += component.current;
        }
        if (total > kUsbBudget) {
            plan_.warnings.push_back(
                {WarningKind::PowerBudget, tr("wiring.power", language_, {{"current", std::to_string(total)}}), {}});
        }
    }

    const BlockCatalog& catalog_;
    const Board& board_;
    Language language_;
    Plan plan_;
    std::map<std::string, std::size_t> index_;               ///< Component key -> index in plan_.
    std::map<std::string, std::set<std::string>> signals_;   ///< Board pin -> keys of the components on it.
    std::map<std::string, std::string> i2c_;                 ///< SDA/SCL pin -> key of a component using I2C.
};

} // namespace

Plan plan(const project::Project& project, const BlockCatalog& catalog, const Board& board, Language language) {
    Planner planner(catalog, board, language);
    for (const project::Script& script : project.scripts) {
        if (script.blocks.empty()) {
            continue;
        }
        const BlockDefinition* first = catalog.find(script.blocks.front().type);
        if (first != nullptr && first->shape == BlockShape::Hat) { // loose blocks do not run
            planner.stack(script.blocks);
        }
    }
    return planner.finish();
}

Plan plan(const project::Project& project, Language language) {
    const Board* board = BoardDatabase::builtin().find(project.boardId);
    if (board == nullptr) {
        return {};
    }
    return plan(project, BlockCatalog::builtin(), *board, language);
}

std::vector<std::string> componentKinds() {
    std::vector<std::string> kinds;
    for (const auto& [kind, description] : components().items()) {
        kinds.push_back(kind);
    }
    return kinds;
}

std::vector<std::string> blockPins(const std::string& kind) {
    std::vector<std::string> pins;
    if (!components().contains(kind)) {
        return pins;
    }
    for (const Json& connection : components().at(kind).value("connections", Json::array())) {
        const std::string to = connection.value("to", std::string{});
        if (!isPower(to)) {
            pins.push_back(to);
        }
    }
    return pins;
}

} // namespace s2a::wiring
