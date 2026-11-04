#include "layout.hpp"

#include <algorithm>
#include <cctype>
#include <optional>

namespace s2a::native {

namespace {

using project::Block;
using project::Stack;
using namespace metrics;

/// One element of a text row, measured before being placed.
struct Item {
    Part part;
    std::vector<BlockGeometry> child; ///< Reporter plugged into a value input, laid out at (0, 0).
};

/// A row of items, or the mouth of a C-block.
struct Row {
    std::optional<std::string> statement;
    std::vector<Item> items;
};

void translate(BlockGeometry& geometry, double dx, double dy) {
    const auto move = [dx, dy](Rect& rect) {
        rect.x += dx;
        rect.y += dy;
    };
    move(geometry.bounds);
    for (Rect& row : geometry.rows) {
        move(row);
    }
    for (Part& part : geometry.parts) {
        move(part.rect);
    }
    for (StatementSlot& slot : geometry.mouths) {
        move(slot.rect);
    }
}

Rect layoutBlock(const LayoutContext& context, const Block& block, Point origin, int script,
                 std::vector<BlockGeometry>& out);

std::string defaultText(const BlockArg& arg) {
    return arg.defaultValue;
}

Item measureArg(const LayoutContext& context, const Block& block, const BlockArg& arg, int script) {
    Item item;
    item.part.name = arg.name;
    item.part.argKind = arg.kind;
    if (arg.kind == ArgKind::Pin || arg.kind == ArgKind::Dropdown || arg.kind == ArgKind::Variable) {
        const auto found = block.fields.find(arg.name);
        const std::string value = found != block.fields.end() && !found->second.empty() ? found->second : defaultText(arg);
        item.part.kind = PartKind::Field;
        item.part.text = fieldText(context, arg, value);
        item.part.rect = {0, 0, context.measure(item.part.text) + 16 + kArrow, kFieldHeight};
        return item;
    }
    item.part.kind = PartKind::ValueInput;
    const auto found = block.inputs.find(arg.name);
    if (found != block.inputs.end() && found->second.block) {
        const Rect size = layoutBlock(context, *found->second.block, {0, 0}, script, item.child);
        item.part.hasBlock = true;
        item.part.rect = {0, 0, size.w, size.h};
        return item;
    }
    if (arg.kind == ArgKind::Boolean) {
        item.part.rect = {0, 0, kEmptyBoolean, kFieldHeight};
        return item;
    }
    item.part.text = found != block.inputs.end() ? found->second.value : defaultText(arg);
    item.part.rect = {0, 0, std::max(28.0, context.measure(item.part.text) + 16), kFieldHeight};
    return item;
}

/// Splits the localized message at its placeholders; statement arguments start a new row.
std::vector<Row> rowsOf(const LayoutContext& context, const Block& block, const BlockDefinition& definition,
                        int script) {
    std::vector<Row> rows(1);
    const std::string& message = definition.message.get(context.language);
    std::string text;
    const auto flushText = [&]() {
        // Trim and drop empty labels.
        const auto first = text.find_first_not_of(' ');
        if (first != std::string::npos) {
            const auto last = text.find_last_not_of(' ');
            Item item;
            item.part.kind = PartKind::Label;
            item.part.text = text.substr(first, last - first + 1);
            item.part.rect = {0, 0, context.measure(item.part.text), kFieldHeight};
            rows.back().items.push_back(std::move(item));
        }
        text.clear();
    };
    for (std::size_t i = 0; i < message.size(); ++i) {
        if (message[i] == '%' && i + 1 < message.size() && std::isdigit(static_cast<unsigned char>(message[i + 1])) != 0) {
            std::size_t end = i + 1;
            while (end < message.size() && std::isdigit(static_cast<unsigned char>(message[end])) != 0) {
                ++end;
            }
            const std::size_t index = std::stoul(message.substr(i + 1, end - i - 1));
            i = end - 1;
            flushText();
            if (index == 0 || index > definition.args.size()) {
                continue;
            }
            const BlockArg& arg = definition.args[index - 1];
            if (arg.kind == ArgKind::Statement) {
                if (rows.back().items.empty() && !rows.back().statement && rows.size() > 1) {
                    rows.pop_back();
                }
                rows.push_back({arg.name, {}});
                rows.emplace_back();
            } else {
                rows.back().items.push_back(measureArg(context, block, arg, script));
            }
        } else {
            text += message[i];
        }
    }
    flushText();
    if (rows.size() > 1 && rows.back().items.empty() && !rows.back().statement) {
        rows.pop_back(); // nothing after the last mouth: the bottom arm is added by the caller
    }
    return rows;
}

Rect layoutBlock(const LayoutContext& context, const Block& block, Point origin, int script,
                 std::vector<BlockGeometry>& out) {
    const std::size_t index = out.size();
    out.emplace_back(); // parents are painted before their children

    BlockGeometry geometry;
    geometry.block = &block;
    geometry.script = script;
    geometry.definition = context.catalog.find(block.type);
    geometry.colour = "#9E9E9E";
    std::vector<Row> rows;
    if (geometry.definition != nullptr) {
        geometry.shape = geometry.definition->shape;
        geometry.terminal = geometry.definition->terminal;
        if (const BlockCategory* category = context.catalog.findCategory(geometry.definition->category)) {
            geometry.colour = category->colour;
        }
        rows = rowsOf(context, block, *geometry.definition, script);
    } else {
        // Unknown block (newer catalog): shown with its type, never an error while editing.
        Item item;
        item.part.text = block.type;
        item.part.rect = {0, 0, context.measure(block.type), kFieldHeight};
        rows.push_back({std::nullopt, {item}});
    }

    const bool reporter = geometry.shape == BlockShape::Reporter || geometry.shape == BlockShape::Boolean;
    double y = origin.y + (geometry.shape == BlockShape::Hat ? kHatHeight : 0);
    double width = 0;
    bool endsWithMouth = false;
    for (Row& row : rows) {
        if (row.statement) {
            StatementSlot slot{*row.statement, {origin.x + kIndent, y, 0, kMinSlot}};
            const auto stack = block.statements.find(*row.statement);
            if (stack != block.statements.end() && !stack->second.empty()) {
                slot.rect.h = std::max(kMinSlot, layoutStack(context, stack->second, {slot.rect.x, y}, script, out));
            }
            y += slot.rect.h;
            geometry.mouths.push_back(slot);
            endsWithMouth = true;
            continue;
        }
        endsWithMouth = false;
        double itemsHeight = 0;
        for (const Item& item : row.items) {
            itemsHeight = std::max(itemsHeight, item.part.rect.h);
        }
        const double height = std::max(reporter ? kReporterHeight : kRowHeight, itemsHeight + 8);
        const double padding = geometry.shape == BlockShape::Boolean ? height / 2 : (reporter ? 12 : kPadding);
        double x = origin.x + padding;
        for (Item& item : row.items) {
            const double dx = x;
            const double dy = y + (height - item.part.rect.h) / 2;
            item.part.rect.x = dx;
            item.part.rect.y = dy;
            for (BlockGeometry& child : item.child) {
                translate(child, dx, dy);
                out.push_back(std::move(child));
            }
            x += item.part.rect.w + kGap;
            geometry.parts.push_back(std::move(item.part));
        }
        if (!row.items.empty()) {
            x -= kGap;
        }
        const double rowWidth = x + padding - origin.x;
        geometry.rows.push_back({origin.x, y, rowWidth, height});
        width = std::max(width, rowWidth);
        y += height;
    }
    if (endsWithMouth) {
        geometry.rows.push_back({origin.x, y, 0, kArm});
        y += kArm;
    }
    width = std::max(width, reporter ? kReporterHeight : kMinWidth);
    if (!geometry.mouths.empty()) {
        width = std::max(width, kIndent + kNotchX + kNotchWidth + 2 * kPadding);
    }
    for (Rect& row : geometry.rows) {
        row.w = width;
    }
    for (StatementSlot& slot : geometry.mouths) {
        slot.rect.w = width - kIndent;
    }
    geometry.bounds = {origin.x, origin.y, width, y - origin.y};
    const Rect bounds = geometry.bounds;
    out[index] = std::move(geometry);
    return bounds;
}

} // namespace

std::string fieldText(const LayoutContext& context, const BlockArg& arg, const std::string& value) {
    if (arg.kind == ArgKind::Dropdown) {
        for (const DropdownOption& option : arg.options) {
            if (option.value == value) {
                return option.label.get(context.language);
            }
        }
    }
    if (arg.kind == ArgKind::Variable) {
        for (const project::Variable& variable : context.project.variables) {
            if (variable.id == value) {
                return variable.name;
            }
        }
        return "?";
    }
    return value.empty() ? std::string("?") : value;
}

double layoutStack(const LayoutContext& context, const Stack& stack, Point origin, int script,
                   std::vector<BlockGeometry>& out) {
    double y = origin.y;
    for (std::size_t i = 0; i < stack.size(); ++i) {
        const std::size_t index = out.size();
        y += layoutBlock(context, stack[i], {origin.x, y}, script, out).h;
        out[index].firstInStack = i == 0;
        out[index].hasNext = i + 1 < stack.size();
    }
    return y - origin.y;
}

std::vector<BlockGeometry> layoutProject(const LayoutContext& context) {
    std::vector<BlockGeometry> blocks;
    for (std::size_t i = 0; i < context.project.scripts.size(); ++i) {
        const project::Script& script = context.project.scripts[i];
        (void)layoutStack(context, script.blocks, {script.x, script.y}, static_cast<int>(i), blocks);
    }
    return blocks;
}

int hitBlock(const std::vector<BlockGeometry>& blocks, Point point) {
    for (std::size_t i = blocks.size(); i-- > 0;) {
        const BlockGeometry& geometry = blocks[i];
        if (!geometry.bounds.contains(point)) {
            continue;
        }
        // The empty part of a C-block mouth is background, not the block.
        const bool inMouth = std::any_of(geometry.mouths.begin(), geometry.mouths.end(),
                                         [point](const StatementSlot& slot) { return slot.rect.contains(point); });
        if (!inMouth) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace s2a::native
