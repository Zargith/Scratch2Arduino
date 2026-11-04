#include "editing.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

namespace s2a::native {

namespace {

using project::Block;
using project::Project;
using project::Stack;

std::optional<Stack> detachFrom(Stack& stack, const std::string& id);

std::optional<Stack> detachInside(Block& block, const std::string& id) {
    for (auto& [name, input] : block.inputs) {
        if (!input.block) {
            continue;
        }
        if (input.block->id == id) {
            Stack removed;
            removed.push_back(std::move(*input.block));
            input.block.reset();
            return removed;
        }
        if (auto removed = detachInside(*input.block, id)) {
            return removed;
        }
    }
    for (auto& [name, stack] : block.statements) {
        if (auto removed = detachFrom(stack, id)) {
            return removed;
        }
    }
    return std::nullopt;
}

std::optional<Stack> detachFrom(Stack& stack, const std::string& id) {
    for (std::size_t i = 0; i < stack.size(); ++i) {
        if (stack[i].id == id) {
            Stack tail(std::make_move_iterator(stack.begin() + static_cast<std::ptrdiff_t>(i)),
                       std::make_move_iterator(stack.end()));
            stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(i), stack.end());
            return tail;
        }
        if (auto removed = detachInside(stack[i], id)) {
            return removed;
        }
    }
    return std::nullopt;
}

Block* findIn(Stack& stack, const std::string& id);

Block* findInside(Block& block, const std::string& id) {
    for (auto& [name, input] : block.inputs) {
        if (input.block) {
            if (input.block->id == id) {
                return input.block.get();
            }
            if (Block* found = findInside(*input.block, id)) {
                return found;
            }
        }
    }
    for (auto& [name, stack] : block.statements) {
        if (Block* found = findIn(stack, id)) {
            return found;
        }
    }
    return nullptr;
}

Block* findIn(Stack& stack, const std::string& id) {
    for (Block& block : stack) {
        if (block.id == id) {
            return &block;
        }
        if (Block* found = findInside(block, id)) {
            return found;
        }
    }
    return nullptr;
}

Block* findBlock(Project& project, const std::string& id) {
    for (project::Script& script : project.scripts) {
        if (Block* found = findIn(script.blocks, id)) {
            return found;
        }
    }
    return nullptr;
}

/// Stack holding block `id` directly, and its index there.
std::pair<Stack*, std::size_t> stackOf(Stack& stack, const std::string& id) {
    for (std::size_t i = 0; i < stack.size(); ++i) {
        if (stack[i].id == id) {
            return {&stack, i};
        }
        for (auto& [name, inner] : stack[i].statements) {
            if (auto found = stackOf(inner, id); found.first != nullptr) {
                return found;
            }
        }
        for (auto& [name, input] : stack[i].inputs) {
            if (!input.block) {
                continue;
            }
            for (auto& [innerName, inner] : input.block->statements) {
                if (auto found = stackOf(inner, id); found.first != nullptr) {
                    return found;
                }
            }
        }
    }
    return {nullptr, 0};
}

double distance(Point a, Point b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

bool isStackShape(BlockShape shape) {
    return shape != BlockShape::Reporter && shape != BlockShape::Boolean;
}

} // namespace

Block makeBlock(const BlockDefinition& definition, const Project& project) {
    Block block;
    block.id = project::newId();
    block.type = definition.type;
    for (const BlockArg& arg : definition.args) {
        switch (arg.kind) {
        case ArgKind::Pin:
        case ArgKind::Dropdown:
            block.fields[arg.name] = arg.defaultValue;
            break;
        case ArgKind::Variable:
            block.fields[arg.name] = project.variables.empty() ? std::string{} : project.variables.front().id;
            break;
        case ArgKind::Number:
        case ArgKind::Text:
            block.inputs[arg.name].value = arg.defaultValue;
            break;
        case ArgKind::Boolean:
        case ArgKind::Statement:
            break;
        }
    }
    return block;
}

std::optional<Stack> detach(Project& project, const std::string& id) {
    for (std::size_t i = 0; i < project.scripts.size(); ++i) {
        if (auto removed = detachFrom(project.scripts[i].blocks, id)) {
            if (project.scripts[i].blocks.empty()) {
                project.scripts.erase(project.scripts.begin() + static_cast<std::ptrdiff_t>(i));
            }
            return removed;
        }
    }
    return std::nullopt;
}

Drop findDrop(const std::vector<BlockGeometry>& blocks, const BlockCatalog& catalog, const Stack& dragged,
              Point origin, double draggedHeight) {
    Drop best;
    best.at = origin;
    if (dragged.empty()) {
        return best;
    }
    const BlockDefinition* first = catalog.find(dragged.front().type);
    const BlockDefinition* last = catalog.find(dragged.back().type);
    const BlockShape shape = first != nullptr ? first->shape : BlockShape::Statement;
    const bool endsTerminal = last != nullptr && last->terminal;
    double bestDistance = kSnapDistance;
    const auto consider = [&](double d, Drop drop) {
        if (d < bestDistance) {
            bestDistance = d;
            drop.at = origin;
            best = std::move(drop);
        }
    };

    if (!isStackShape(shape)) {
        // Reporters go into value inputs of the right kind (conditions take booleans only).
        const Point plug{origin.x, origin.y + metrics::kFieldHeight / 2};
        for (const BlockGeometry& geometry : blocks) {
            for (const Part& part : geometry.parts) {
                if (part.kind != PartKind::ValueInput) {
                    continue;
                }
                const bool fits = shape == BlockShape::Boolean ? part.argKind == ArgKind::Boolean
                                                               : part.argKind != ArgKind::Boolean;
                if (fits) {
                    consider(distance(plug, {part.rect.x, part.rect.y + part.rect.h / 2}),
                             {Drop::Kind::PlugInput, geometry.block->id, part.name, geometry.script, {}});
                }
            }
        }
        return best;
    }

    const bool isHat = shape == BlockShape::Hat;
    for (const BlockGeometry& geometry : blocks) {
        if (!isStackShape(geometry.shape)) {
            continue;
        }
        // Below a block: its followers go below the dragged stack, unless it ends with "forever".
        if (!isHat && !geometry.terminal && !(endsTerminal && geometry.hasNext)) {
            consider(distance(origin, {geometry.bounds.x, geometry.bounds.bottom()}),
                     {Drop::Kind::After, geometry.block->id, {}, geometry.script, {}});
        }
        // Above the first block of a script.
        if (geometry.firstInStack && geometry.script >= 0 && geometry.shape != BlockShape::Hat && !endsTerminal) {
            const bool scriptTop = std::none_of(blocks.begin(), blocks.end(), [&](const BlockGeometry& other) {
                return other.script == geometry.script && &other < &geometry;
            });
            if (scriptTop) {
                consider(distance(origin, {geometry.bounds.x, geometry.bounds.y - draggedHeight}),
                         {Drop::Kind::Before, geometry.block->id, {}, geometry.script, {}});
            }
        }
        // Into a C-block mouth.
        if (!isHat) {
            for (const StatementSlot& slot : geometry.slots) {
                consider(distance(origin, {slot.rect.x, slot.rect.y}),
                         {Drop::Kind::Into, geometry.block->id, slot.name, geometry.script, {}});
            }
        }
    }
    return best;
}

void applyDrop(Project& project, const Drop& drop, Stack dragged) {
    if (dragged.empty()) {
        return;
    }
    const auto asNewScript = [&project, &drop](Stack blocks) {
        project.scripts.push_back({drop.at.x, drop.at.y, std::move(blocks)});
    };
    switch (drop.kind) {
    case Drop::Kind::NewScript:
        asNewScript(std::move(dragged));
        return;
    case Drop::Kind::After:
        for (project::Script& script : project.scripts) {
            if (auto [stack, index] = stackOf(script.blocks, drop.target); stack != nullptr) {
                stack->insert(stack->begin() + static_cast<std::ptrdiff_t>(index + 1),
                              std::make_move_iterator(dragged.begin()), std::make_move_iterator(dragged.end()));
                return;
            }
        }
        break;
    case Drop::Kind::Before:
        if (drop.script >= 0 && static_cast<std::size_t>(drop.script) < project.scripts.size()) {
            project::Script& script = project.scripts[static_cast<std::size_t>(drop.script)];
            if (!script.blocks.empty() && script.blocks.front().id == drop.target) {
                script.blocks.insert(script.blocks.begin(), std::make_move_iterator(dragged.begin()),
                                     std::make_move_iterator(dragged.end()));
                script.x = drop.at.x;
                script.y = drop.at.y;
                return;
            }
        }
        break;
    case Drop::Kind::Into:
        if (Block* target = findBlock(project, drop.target)) {
            Stack& mouth = target->statements[drop.name];
            mouth.insert(mouth.begin(), std::make_move_iterator(dragged.begin()), std::make_move_iterator(dragged.end()));
            return;
        }
        break;
    case Drop::Kind::PlugInput:
        if (Block* target = findBlock(project, drop.target)) {
            project::Input& input = target->inputs[drop.name];
            if (input.block) {
                // The reporter already there is pushed out, next to the drop point.
                Stack ejected;
                ejected.push_back(std::move(*input.block));
                project.scripts.push_back({drop.at.x + 24, drop.at.y + 48, std::move(ejected)});
            }
            input.block = std::move(dragged.front());
            return;
        }
        break;
    }
    asNewScript(std::move(dragged));
}

} // namespace s2a::native
