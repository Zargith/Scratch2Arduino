#include "editing.hpp"

#include <gtest/gtest.h>

namespace {

using namespace s2a::native;
using s2a::BlockCatalog;
using s2a::project::Block;
using s2a::project::Project;
using s2a::project::Stack;

Block block(std::string id, std::string type) {
    Block result;
    result.id = std::move(id);
    result.type = std::move(type);
    return result;
}

LayoutContext context(const Project& project) {
    return {BlockCatalog::builtin(), project, nullptr, s2a::Language::English,
            [](const std::string& text) { return 7.0 * static_cast<double>(text.size()); }};
}

/// hat, a, b at (0, 0); a lone repeat (empty mouth) at (400, 0).
Project sample() {
    Project project;
    project.scripts.push_back(
        {0, 0, {block("hat", "events_when_program_starts"), block("a", "control_wait"), block("b", "control_wait")}});
    project.scripts.push_back({400, 0, {block("repeat", "control_repeat")}});
    return project;
}

std::vector<std::string> ids(const Stack& stack) {
    std::vector<std::string> result;
    for (const Block& item : stack) {
        result.push_back(item.id);
    }
    return result;
}

const BlockGeometry& byId(const std::vector<BlockGeometry>& blocks, const char* id) {
    for (const auto& geometry : blocks) {
        if (geometry.block->id == id) {
            return geometry;
        }
    }
    throw std::runtime_error(std::string("no block ") + id);
}

TEST(Editing, MakesBlocksWithDefaultValues) {
    Project project;
    project.variables.push_back({"v1", "score", 0});
    const Block write = makeBlock(*BlockCatalog::builtin().find("pins_digital_write"), project);
    EXPECT_FALSE(write.id.empty());
    EXPECT_EQ(write.fields.at("PIN"), "13");
    EXPECT_EQ(write.fields.at("STATE"), "HIGH");
    const Block set = makeBlock(*BlockCatalog::builtin().find("variables_set"), project);
    EXPECT_EQ(set.fields.at("VARIABLE"), "v1");
    EXPECT_EQ(set.inputs.at("VALUE").value, "0");
}

TEST(Editing, DetachesABlockWithTheBlocksBelowIt) {
    Project project = sample();
    const auto removed = detach(project, "a");
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(ids(*removed), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(ids(project.scripts[0].blocks), std::vector<std::string>{"hat"});
    // Taking the first block takes the whole script.
    ASSERT_TRUE(detach(project, "hat").has_value());
    EXPECT_EQ(project.scripts.size(), 1U);
    EXPECT_FALSE(detach(project, "nope").has_value());
}

TEST(Editing, DetachesAReporterFromItsInput) {
    Project project;
    Block wait = block("wait", "control_wait");
    wait.inputs["SECONDS"].value = "2";
    wait.inputs["SECONDS"].block = block("sum", "operators_add");
    project.scripts.push_back({0, 0, {wait}});
    const auto removed = detach(project, "sum");
    ASSERT_TRUE(removed.has_value());
    EXPECT_EQ(ids(*removed), std::vector<std::string>{"sum"});
    const auto& input = project.scripts[0].blocks[0].inputs.at("SECONDS");
    EXPECT_FALSE(input.block);
    EXPECT_EQ(input.value, "2"); // the typed value comes back
}

TEST(Editing, SnapsBelowABlockAndInsertsInTheMiddle) {
    Project project = sample();
    const auto blocks = layoutProject(context(project));
    const Rect a = byId(blocks, "a").bounds;
    const Stack dragged{block("new", "pins_builtin_led")};
    const Drop drop = findDrop(blocks, BlockCatalog::builtin(), dragged, {a.x + 5, a.bottom() + 6}, 40);
    EXPECT_EQ(drop.kind, Drop::Kind::After);
    EXPECT_EQ(drop.target, "a");
    applyDrop(project, drop, dragged);
    EXPECT_EQ(ids(project.scripts[0].blocks), (std::vector<std::string>{"hat", "a", "new", "b"}));
}

TEST(Editing, DropsFarAwayAsANewScript) {
    Project project = sample();
    const auto blocks = layoutProject(context(project));
    const Stack dragged{block("new", "pins_builtin_led")};
    const Drop drop = findDrop(blocks, BlockCatalog::builtin(), dragged, {800, 600}, 40);
    EXPECT_EQ(drop.kind, Drop::Kind::NewScript);
    applyDrop(project, drop, dragged);
    ASSERT_EQ(project.scripts.size(), 3U);
    EXPECT_DOUBLE_EQ(project.scripts[2].x, 800);
}

TEST(Editing, SnapsIntoAnEmptyMouth) {
    Project project = sample();
    const auto blocks = layoutProject(context(project));
    const Rect mouth = byId(blocks, "repeat").mouths.at(0).rect;
    const Stack dragged{block("new", "pins_builtin_led")};
    const Drop drop = findDrop(blocks, BlockCatalog::builtin(), dragged, {mouth.x + 3, mouth.y + 3}, 40);
    EXPECT_EQ(drop.kind, Drop::Kind::Into);
    applyDrop(project, drop, dragged);
    EXPECT_EQ(ids(project.scripts[1].blocks[0].statements.at("SUBSTACK")), std::vector<std::string>{"new"});
}

TEST(Editing, SnapsAboveAScriptWithoutHat) {
    Project project = sample();
    const auto blocks = layoutProject(context(project));
    const Rect repeat = byId(blocks, "repeat").bounds;
    const Stack dragged{block("new", "pins_builtin_led")};
    const Drop drop = findDrop(blocks, BlockCatalog::builtin(), dragged, {repeat.x, repeat.y - 40}, 40);
    EXPECT_EQ(drop.kind, Drop::Kind::Before);
    applyDrop(project, drop, dragged);
    EXPECT_EQ(ids(project.scripts[1].blocks), (std::vector<std::string>{"new", "repeat"}));
    EXPECT_DOUBLE_EQ(project.scripts[1].y, repeat.y - 40);
}

TEST(Editing, ForeverCannotCutAStackInTwo) {
    Project project = sample();
    const auto blocks = layoutProject(context(project));
    const Rect a = byId(blocks, "a").bounds;
    const Stack dragged{block("loop", "control_forever")};
    // Below "a" (which has "b" below it): not allowed. Below "b" (the end): allowed.
    EXPECT_NE(findDrop(blocks, BlockCatalog::builtin(), dragged, {a.x, a.bottom()}, 80).target, "a");
    const Rect b = byId(blocks, "b").bounds;
    const Drop drop = findDrop(blocks, BlockCatalog::builtin(), dragged, {b.x, b.bottom()}, 80);
    EXPECT_EQ(drop.kind, Drop::Kind::After);
    EXPECT_EQ(drop.target, "b");
}

TEST(Editing, PlugsReportersIntoMatchingInputsOnly) {
    Project project = sample();
    const auto blocks = layoutProject(context(project));
    const BlockGeometry& a = byId(blocks, "a");
    const Rect seconds = a.parts.at(1).rect;
    ASSERT_EQ(a.parts.at(1).name, "SECONDS");
    const Point near{seconds.x + 2, seconds.y + seconds.h / 2 - metrics::kFieldHeight / 2};

    const Stack boolean{block("lt", "operators_lt")};
    EXPECT_EQ(findDrop(blocks, BlockCatalog::builtin(), boolean, near, 32).kind, Drop::Kind::NewScript);

    const Stack number{block("sum", "operators_add")};
    const Drop drop = findDrop(blocks, BlockCatalog::builtin(), number, near, 32);
    ASSERT_EQ(drop.kind, Drop::Kind::PlugInput);
    applyDrop(project, drop, number);
    EXPECT_EQ(project.scripts[0].blocks[1].inputs.at("SECONDS").block->id, "sum");

    // A second reporter pushes the first one out.
    applyDrop(project, drop, Stack{block("product", "operators_multiply")});
    EXPECT_EQ(project.scripts[0].blocks[1].inputs.at("SECONDS").block->id, "product");
    EXPECT_EQ(ids(project.scripts.back().blocks), std::vector<std::string>{"sum"});
}

} // namespace
