#include "s2a/blocks.hpp"

#include "s2a/resources.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

namespace s2a {

namespace {

using Json = nlohmann::json;

/// Collects every problem of the catalog instead of stopping at the first one.
class Issues {
public:
    void add(const std::string& where, const std::string& what) { lines_.push_back(where + ": " + what); }
    [[nodiscard]] bool empty() const noexcept { return lines_.empty(); }
    [[nodiscard]] std::string joined() const {
        std::string text;
        for (const auto& line : lines_) {
            text += line + "\n";
        }
        return text;
    }

private:
    std::vector<std::string> lines_;
};

bool isIdentifier(std::string_view text, bool upper) {
    if (text.empty()) {
        return false;
    }
    const auto letter = [upper](char c) {
        const auto u = static_cast<unsigned char>(c);
        return upper ? std::isupper(u) != 0 : std::islower(u) != 0;
    };
    return letter(text.front()) && std::all_of(text.begin(), text.end(), [&](char c) {
               return letter(c) || std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '_';
           });
}

bool isColour(std::string_view text) {
    return text.size() == 7 && text[0] == '#' &&
           std::all_of(text.begin() + 1, text.end(),
                       [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; });
}

std::optional<std::string> stringField(const Json& node, const char* key) {
    if (node.contains(key) && node.at(key).is_string()) {
        return node.at(key).get<std::string>();
    }
    return std::nullopt;
}

LocalizedText localized(const Json& node, const char* key, const std::string& where, Issues& issues) {
    LocalizedText text;
    if (!node.contains(key) || !node.at(key).is_object()) {
        issues.add(where, std::string("'") + key + "' must be an object with 'fr' and 'en'");
        return text;
    }
    const Json& value = node.at(key);
    text.french = stringField(value, "fr").value_or("");
    text.english = stringField(value, "en").value_or("");
    if (text.french.empty() || text.english.empty()) {
        issues.add(where, std::string("'") + key + "' needs non-empty 'fr' and 'en' texts");
    }
    return text;
}

/// Checks that `message` uses %1..%argCount, each exactly once, and nothing else.
void checkPlaceholders(const std::string& message, std::size_t argCount, const std::string& where,
                       Issues& issues) {
    std::vector<int> seen(argCount + 1, 0);
    for (std::size_t pos = message.find('%'); pos != std::string::npos; pos = message.find('%', pos + 1)) {
        std::size_t end = pos + 1;
        while (end < message.size() && std::isdigit(static_cast<unsigned char>(message[end])) != 0) {
            ++end;
        }
        if (end == pos + 1) {
            issues.add(where, "'%' must be followed by an argument number");
            continue;
        }
        // Bounded parsing: no block has thousands of arguments, and std::stoul would throw on overflow.
        constexpr std::size_t kMaxDigits = 4;
        const std::string digits = message.substr(pos + 1, end - pos - 1);
        if (digits.size() > kMaxDigits) {
            issues.add(where, "placeholder %" + digits + " has no matching argument");
            continue;
        }
        const auto index = static_cast<std::size_t>(std::stoul(digits));
        if (index == 0 || index > argCount) {
            issues.add(where, "placeholder %" + std::to_string(index) + " has no matching argument");
        } else {
            ++seen[index];
        }
    }
    for (std::size_t index = 1; index <= argCount; ++index) {
        if (seen[index] != 1) {
            issues.add(where, "placeholder %" + std::to_string(index) + " must appear exactly once");
        }
    }
}

std::optional<BlockShape> parseShape(std::string_view text) {
    static const std::map<std::string_view, BlockShape> shapes{
        {"hat", BlockShape::Hat},           {"statement", BlockShape::Statement},
        {"c", BlockShape::CBlock},          {"reporter", BlockShape::Reporter},
        {"boolean", BlockShape::Boolean}};
    const auto it = shapes.find(text);
    return it != shapes.end() ? std::optional(it->second) : std::nullopt;
}

std::optional<ArgKind> parseKind(std::string_view text) {
    static const std::map<std::string_view, ArgKind> kinds{
        {"number", ArgKind::Number},     {"text", ArgKind::Text},         {"boolean", ArgKind::Boolean},
        {"pin", ArgKind::Pin},           {"dropdown", ArgKind::Dropdown}, {"variable", ArgKind::Variable}, {"function", ArgKind::Function},
        {"statement", ArgKind::Statement}};
    const auto it = kinds.find(text);
    return it != kinds.end() ? std::optional(it->second) : std::nullopt;
}

std::optional<PinCapability> parseCapability(std::string_view text) {
    static const std::map<std::string_view, PinCapability> capabilities{
        {"digital", PinCapability::Digital},
        {"analog", PinCapability::Analog},
        {"pwm", PinCapability::Pwm},
        {"interrupt", PinCapability::Interrupt}};
    const auto it = capabilities.find(text);
    return it != capabilities.end() ? std::optional(it->second) : std::nullopt;
}

BlockArg parseArg(const Json& node, const std::string& where, Issues& issues) {
    BlockArg arg;
    arg.name = stringField(node, "name").value_or("");
    if (!isIdentifier(arg.name, /*upper=*/true)) {
        issues.add(where, "argument name '" + arg.name + "' must be UPPER_CASE");
    }
    const std::string at = where + "/" + arg.name;
    const auto kind = parseKind(stringField(node, "kind").value_or(""));
    if (!kind) {
        issues.add(at, "unknown argument kind");
        return arg;
    }
    arg.kind = *kind;
    if (node.contains("default")) {
        const Json& value = node.at("default");
        if (value.is_string()) {
            arg.defaultValue = value.get<std::string>();
        } else if (value.is_number()) {
            arg.defaultValue = value.dump();
        } else {
            issues.add(at, "'default' must be a number or a string");
        }
    }
    for (const char* bound : {"min", "max"}) {
        if (node.contains(bound)) {
            if (arg.kind != ArgKind::Number || !node.at(bound).is_number()) {
                issues.add(at, std::string("'") + bound + "' is only allowed as a number on number arguments");
            } else {
                (std::string_view(bound) == "min" ? arg.min : arg.max) = node.at(bound).get<double>();
            }
        }
    }
    if (arg.min && arg.max && *arg.min > *arg.max) {
        issues.add(at, "'min' is greater than 'max'");
    }
    if (arg.kind == ArgKind::Pin) {
        arg.pinCapability = parseCapability(stringField(node, "capability").value_or(""));
        if (!arg.pinCapability) {
            issues.add(at, "pin arguments need a 'capability' (digital, analog, pwm, interrupt)");
        }
    } else if (node.contains("capability")) {
        issues.add(at, "'capability' is only allowed on pin arguments");
    }
    if (arg.kind == ArgKind::Dropdown) {
        if (!node.contains("options") || !node.at("options").is_array() || node.at("options").empty()) {
            issues.add(at, "dropdown arguments need at least one option");
        } else {
            std::set<std::string> values;
            for (const Json& option : node.at("options")) {
                DropdownOption parsed{stringField(option, "value").value_or(""),
                                      localized(option, "label", at, issues)};
                if (parsed.value.empty() || !values.insert(parsed.value).second) {
                    issues.add(at, "option values must be unique and non-empty");
                }
                arg.options.push_back(std::move(parsed));
            }
            if (!arg.defaultValue.empty() && values.count(arg.defaultValue) == 0) {
                issues.add(at, "default '" + arg.defaultValue + "' is not one of the options");
            }
        }
    } else if (node.contains("options")) {
        issues.add(at, "'options' is only allowed on dropdown arguments");
    }
    if ((arg.kind == ArgKind::Statement || arg.kind == ArgKind::Boolean || arg.kind == ArgKind::Variable ||
         arg.kind == ArgKind::Function) &&
        !arg.defaultValue.empty()) {
        issues.add(at, "this argument kind cannot have a default value");
    }
    return arg;
}

void checkShape(const BlockDefinition& block, const std::string& where, Issues& issues) {
    const auto statements = std::count_if(block.args.begin(), block.args.end(),
                                          [](const BlockArg& arg) { return arg.kind == ArgKind::Statement; });
    if (block.shape == BlockShape::CBlock && statements == 0) {
        issues.add(where, "a 'c' block needs at least one statement argument");
    }
    if (block.shape != BlockShape::CBlock && statements > 0) {
        issues.add(where, "only 'c' blocks can have statement arguments");
    }
    if (block.terminal && !block.isStackable()) {
        issues.add(where, "reporters and booleans cannot be terminal");
    }
}

BlockDefinition parseBlock(const Json& node, const std::string& category, const std::string& file,
                           Issues& issues) {
    BlockDefinition block;
    block.category = category;
    block.type = stringField(node, "type").value_or("");
    const std::string where = file + "/" + (block.type.empty() ? "?" : block.type);
    if (!isIdentifier(block.type, /*upper=*/false)) {
        issues.add(where, "block type must be lower_snake_case");
    }
    if (block.type.rfind(category + "_", 0) != 0) {
        issues.add(where, "block type must start with its category id '" + category + "_'");
    }
    const auto shape = parseShape(stringField(node, "shape").value_or(""));
    if (!shape) {
        issues.add(where, "unknown shape (hat, statement, c, reporter, boolean)");
    }
    block.shape = shape.value_or(BlockShape::Statement);
    if (node.contains("terminal")) {
        if (!node.at("terminal").is_boolean()) {
            issues.add(where, "'terminal' must be true or false");
        } else {
            block.terminal = node.at("terminal").get<bool>();
        }
    }
    block.message = localized(node, "message", where, issues);
    block.tooltip = localized(node, "tooltip", where, issues);

    if (node.contains("args")) {
        if (!node.at("args").is_array()) {
            issues.add(where, "'args' must be an array");
        } else {
            std::set<std::string> names;
            for (const Json& argNode : node.at("args")) {
                if (!argNode.is_object()) {
                    issues.add(where, "every argument must be an object");
                    continue;
                }
                BlockArg arg = parseArg(argNode, where, issues);
                if (!names.insert(arg.name).second) {
                    issues.add(where, "argument '" + arg.name + "' is defined twice");
                }
                block.args.push_back(std::move(arg));
            }
        }
    }
    checkPlaceholders(block.message.french, block.args.size(), where + " (fr)", issues);
    checkPlaceholders(block.message.english, block.args.size(), where + " (en)", issues);
    checkShape(block, where, issues);
    return block;
}

} // namespace

const BlockArg* BlockDefinition::findArg(std::string_view name) const {
    const auto it = std::find_if(args.begin(), args.end(), [&](const BlockArg& arg) { return arg.name == name; });
    return it != args.end() ? &*it : nullptr;
}

Result<BlockCatalog> BlockCatalog::fromJson(const std::vector<std::string_view>& documents) {
    BlockCatalog catalog;
    Issues issues;
    std::vector<std::pair<int, std::vector<BlockDefinition>>> blocksByCategory;

    for (std::size_t index = 0; index < documents.size(); ++index) {
        const std::string file = "catalog #" + std::to_string(index);
        const Json json = Json::parse(documents[index], nullptr, /*allow_exceptions=*/false);
        if (json.is_discarded() || !json.is_object()) {
            issues.add(file, "not a JSON object");
            continue;
        }
        if (!json.contains("category") || !json.at("category").is_object()) {
            issues.add(file, "missing 'category' object");
            continue;
        }
        const Json& categoryNode = json.at("category");
        BlockCategory category;
        category.id = stringField(categoryNode, "id").value_or("");
        const std::string where = file + " (" + category.id + ")";
        if (!isIdentifier(category.id, /*upper=*/false)) {
            issues.add(where, "category id must be lower_snake_case");
        }
        category.colour = stringField(categoryNode, "colour").value_or("");
        if (!isColour(category.colour)) {
            issues.add(where, "colour must look like #RRGGBB");
        }
        category.name = localized(categoryNode, "name", where, issues);
        if (!categoryNode.contains("order") || !categoryNode.at("order").is_number_integer()) {
            issues.add(where, "'order' must be an integer");
        } else {
            category.order = categoryNode.at("order").get<int>();
        }
        if (catalog.findCategory(category.id) != nullptr) {
            issues.add(where, "category '" + category.id + "' is defined twice");
        }

        std::vector<BlockDefinition> blocks;
        if (!json.contains("blocks") || !json.at("blocks").is_array()) {
            issues.add(where, "'blocks' must be an array");
        } else {
            for (const Json& blockNode : json.at("blocks")) {
                if (!blockNode.is_object()) {
                    issues.add(where, "every block must be an object");
                    continue;
                }
                blocks.push_back(parseBlock(blockNode, category.id, file, issues));
            }
        }
        blocksByCategory.emplace_back(category.order, std::move(blocks));
        catalog.categories_.push_back(std::move(category));
    }

    std::stable_sort(catalog.categories_.begin(), catalog.categories_.end(),
                     [](const BlockCategory& a, const BlockCategory& b) { return a.order < b.order; });
    std::stable_sort(blocksByCategory.begin(), blocksByCategory.end(),
                     [](const auto& a, const auto& b) { return a.first < b.first; });
    std::set<std::string> types;
    for (auto& [order, blocks] : blocksByCategory) {
        for (auto& block : blocks) {
            if (!types.insert(block.type).second) {
                issues.add(block.type, "block type is defined twice");
            }
            catalog.blocks_.push_back(std::move(block));
        }
    }

    if (!issues.empty()) {
        return Error{ErrorCode::InvalidCatalog, issues.joined()};
    }
    return catalog;
}

const BlockCatalog& BlockCatalog::builtin() {
    static const BlockCatalog catalog = [] {
        std::vector<std::string_view> documents;
        for (const auto& resource : resources::list("blocks/catalog/")) {
            documents.push_back(resource.data);
        }
        auto result = fromJson(documents);
        // The embedded catalog is validated by the unit tests; never crash the application.
        return result.ok() ? std::move(result).value() : BlockCatalog{};
    }();
    return catalog;
}

const BlockDefinition* BlockCatalog::find(std::string_view type) const {
    const auto it = std::find_if(blocks_.begin(), blocks_.end(),
                                 [&](const BlockDefinition& block) { return block.type == type; });
    return it != blocks_.end() ? &*it : nullptr;
}

const BlockCategory* BlockCatalog::findCategory(std::string_view id) const {
    const auto it = std::find_if(categories_.begin(), categories_.end(),
                                 [&](const BlockCategory& category) { return category.id == id; });
    return it != categories_.end() ? &*it : nullptr;
}

std::vector<const BlockDefinition*> BlockCatalog::blocksIn(std::string_view category) const {
    std::vector<const BlockDefinition*> result;
    for (const auto& block : blocks_) {
        if (block.category == category) {
            result.push_back(&block);
        }
    }
    return result;
}

} // namespace s2a
