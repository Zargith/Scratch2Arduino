#pragma once

#include "s2a/blocks.hpp"
#include "s2a/boards.hpp"
#include "s2a/i18n.hpp"
#include "s2a/project.hpp"

#include <functional>
#include <string>
#include <vector>

/// Geometry of blocks on the canvas, computed from the project model. Pure C++ (no Qt) so it
/// is unit-tested; the canvas paints and hit-tests from it. Units are canvas pixels at zoom 1.
namespace s2a::native {

struct Point {
    double x = 0;
    double y = 0;
};

struct Rect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;

    [[nodiscard]] double right() const { return x + w; }
    [[nodiscard]] double bottom() const { return y + h; }
    [[nodiscard]] bool contains(Point p) const { return p.x >= x && p.x <= right() && p.y >= y && p.y <= bottom(); }
};

namespace metrics {
inline constexpr double kRowHeight = 40;      ///< Statement row.
inline constexpr double kReporterHeight = 32; ///< Reporter / boolean with plain fields.
inline constexpr double kFieldHeight = 24;    ///< Field and empty input boxes.
inline constexpr double kPadding = 10;        ///< Inner horizontal margin of stack blocks.
inline constexpr double kGap = 6;             ///< Between items of a row.
inline constexpr double kHatHeight = 16;      ///< Bump above hat blocks.
inline constexpr double kIndent = 16;         ///< Left arm of C-blocks.
inline constexpr double kMinSlot = 24;        ///< Height of an empty C-block mouth.
inline constexpr double kArm = 24;            ///< Bottom arm of C-blocks.
inline constexpr double kMinWidth = 64;       ///< Narrowest stack block.
inline constexpr double kArrow = 12;          ///< Dropdown arrow of menu fields.
inline constexpr double kEmptyBoolean = 40;   ///< Empty condition slot.
inline constexpr double kNotchX = 16;         ///< Notch (stack connection) offset and size.
inline constexpr double kNotchWidth = 16;
inline constexpr double kNotchDepth = 4;
} // namespace metrics

enum class PartKind {
    Label,      ///< Text of the block.
    Field,      ///< Menu: pin, dropdown, variable.
    ValueInput, ///< Number / text / condition slot (empty or holding a block).
};

struct Part {
    PartKind kind = PartKind::Label;
    std::string name; ///< Argument name (fields and inputs).
    ArgKind argKind = ArgKind::Number;
    std::string text; ///< What is shown (label, field text, typed value).
    Rect rect;
    bool hasBlock = false; ///< ValueInput holding a reporter.
};

struct StatementSlot {
    std::string name;
    Rect rect; ///< Mouth of the C-block: blocks attach at (x, y).
};

struct BlockGeometry {
    const project::Block* block = nullptr;
    const BlockDefinition* definition = nullptr; ///< Null for unknown types (drawn grey).
    BlockShape shape = BlockShape::Statement;
    bool terminal = false;
    std::string colour; ///< "#RRGGBB"
    Rect bounds;        ///< The block itself, C-block mouths included, following blocks excluded.
    std::vector<Rect> rows; ///< Text rows (C-blocks: one per arm), for painting.
    std::vector<Part> parts;
    std::vector<StatementSlot> slots;
    int script = -1; ///< Index of the script, -1 for blocks outside the project (palette, drag).
    bool firstInStack = false; ///< First block of a script or of a C-block mouth.
    bool hasNext = false;      ///< Another block is attached below.
};

using Measure = std::function<double(const std::string&)>;

struct LayoutContext {
    const BlockCatalog& catalog;
    const project::Project& project; ///< Variable names.
    const Board* board = nullptr;
    Language language = Language::French;
    Measure measure; ///< Width of a text in pixels.
};

/// Lays out a stack at `origin`; geometries are appended in paint order (parents before children).
/// Returns the stack height.
double layoutStack(const LayoutContext& context, const project::Stack& stack, Point origin, int script,
                   std::vector<BlockGeometry>& out);

/// Every script of the project, in paint order.
[[nodiscard]] std::vector<BlockGeometry> layoutProject(const LayoutContext& context);

/// Text shown by a field: localized dropdown label, pin label, variable name.
[[nodiscard]] std::string fieldText(const LayoutContext& context, const BlockArg& arg, const std::string& value);

/// Topmost block under `point` (searching from the end), or -1.
[[nodiscard]] int hitBlock(const std::vector<BlockGeometry>& blocks, Point point);

} // namespace s2a::native
