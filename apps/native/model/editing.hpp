#pragma once

#include "layout.hpp"

#include "s2a/blocks.hpp"
#include "s2a/project.hpp"

#include <optional>
#include <string>
#include <vector>

/// Changes made to the project by drag and drop. Pure C++ (no Qt), unit-tested. Blocks are
/// found by id (unique in a project). None of these functions reports an error to the user:
/// a move that does not apply simply leaves the blocks where they are.
namespace s2a::native {

/// New block of `definition` with its default field and input values.
[[nodiscard]] project::Block makeBlock(const BlockDefinition& definition, const project::Project& project);

/// Removes the block `id` and the blocks below it in its stack (or the reporter from its input)
/// and returns them. Empty scripts are removed.
[[nodiscard]] std::optional<project::Stack> detach(project::Project& project, const std::string& id);

/// Where a dragged stack (or reporter) is dropped.
struct Drop {
    enum class Kind {
        NewScript,   ///< On the background, at `at`.
        After,       ///< Below block `target`.
        Before,      ///< Above the first block of script `script`.
        Into,        ///< First in the mouth `name` of block `target`.
        PlugInput,   ///< Reporter into the input `name` of block `target`.
    };
    Kind kind = Kind::NewScript;
    std::string target;
    std::string name;
    int script = -1;
    Point at;
};

/// Best drop for `dragged`, whose first block's top-left corner is at `origin`, given the
/// geometry of the project (the dragged blocks are not part of it).
[[nodiscard]] Drop findDrop(const std::vector<BlockGeometry>& blocks, const BlockCatalog& catalog,
                            const project::Stack& dragged, Point origin, double draggedHeight);

/// Applies a drop. A reporter plugged into an occupied input pushes the old one out to a new
/// script next to it.
void applyDrop(project::Project& project, const Drop& drop, project::Stack dragged);

inline constexpr double kSnapDistance = 28;

} // namespace s2a::native
