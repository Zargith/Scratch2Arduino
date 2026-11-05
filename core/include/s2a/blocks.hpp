#pragma once

#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/result.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace s2a {

/// Text available in every interface language.
struct LocalizedText {
    std::string french;
    std::string english;

    [[nodiscard]] const std::string& get(Language language) const noexcept {
        return language == Language::English ? english : french;
    }
};

/// How a block looks and connects (see blocks/schema/catalog.schema.json).
enum class BlockShape {
    Hat,       ///< Starts a script; blocks attach below.
    Statement, ///< Stacks above and below.
    CBlock,    ///< Statement wrapping one or more stacks (loops, conditions).
    Reporter,  ///< Returns a number or text; plugs into inputs.
    Boolean,   ///< Returns true or false; plugs into conditions.
};

enum class ArgKind {
    Number,    ///< Number slot (accepts reporters).
    Text,      ///< Text slot (accepts reporters).
    Boolean,   ///< Condition slot (accepts boolean blocks).
    Pin,       ///< Pin menu, filtered by capability for the selected board.
    Dropdown,  ///< Fixed menu.
    Variable,  ///< Variable menu.
    Statement, ///< Stack of blocks inside a C-block.
};

struct DropdownOption {
    std::string value;
    LocalizedText label;
    std::optional<LocalizedText> tooltip; ///< Shown over the menu when this option is chosen.
};

struct BlockArg {
    std::string name;
    ArgKind kind = ArgKind::Number;
    std::string defaultValue; ///< Empty when there is no default.
    std::optional<double> min;
    std::optional<double> max;
    std::optional<PinCapability> pinCapability; ///< Pin arguments only.
    std::vector<DropdownOption> options;        ///< Dropdown arguments only.
};

struct BlockDefinition {
    std::string type;
    std::string category;
    BlockShape shape = BlockShape::Statement;
    bool terminal = false; ///< Nothing attaches below (forever).
    LocalizedText message; ///< With %1..%n placeholders, one per argument.
    LocalizedText tooltip;
    std::vector<BlockArg> args;

    [[nodiscard]] const BlockArg* findArg(std::string_view name) const;
    /// True for hats, statements and C-blocks: they are part of a stack.
    [[nodiscard]] bool isStackable() const noexcept {
        return shape != BlockShape::Reporter && shape != BlockShape::Boolean;
    }
};

struct BlockCategory {
    std::string id;
    std::string colour; ///< "#RRGGBB"
    LocalizedText name;
    int order = 0;
};

/// Every block the editors can show, loaded from blocks/catalog/*.json.
class BlockCatalog {
public:
    /// Parses and checks catalog files (one JSON document per category). All problems are
    /// reported at once in the error detail, one per line.
    [[nodiscard]] static Result<BlockCatalog> fromJson(const std::vector<std::string_view>& documents);

    /// Catalog embedded in the application.
    [[nodiscard]] static const BlockCatalog& builtin();

    /// Sorted by toolbox order.
    [[nodiscard]] const std::vector<BlockCategory>& categories() const noexcept { return categories_; }
    /// Sorted by category order, then in file order.
    [[nodiscard]] const std::vector<BlockDefinition>& blocks() const noexcept { return blocks_; }

    [[nodiscard]] const BlockDefinition* find(std::string_view type) const;
    [[nodiscard]] const BlockCategory* findCategory(std::string_view id) const;
    [[nodiscard]] std::vector<const BlockDefinition*> blocksIn(std::string_view category) const;

private:
    std::vector<BlockCategory> categories_;
    std::vector<BlockDefinition> blocks_;
};

} // namespace s2a
