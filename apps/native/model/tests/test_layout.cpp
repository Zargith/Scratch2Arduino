#include "layout.hpp"

#include <algorithm>
#include <gtest/gtest.h>
#include <stdexcept>

namespace {

using namespace s2a::native;
using s2a::BlockCatalog;
using s2a::project::Block;
using s2a::project::Project;

Block block(std::string id, std::string type) {
    Block result;
    result.id = std::move(id);
    result.type = std::move(type);
    return result;
}

Project blink() {
    Project project;
    project.boardId = "uno";
    Block on = block("on", "pins_builtin_led");
    on.fields["STATE"] = "ON";
    Block wait = block("wait", "control_wait");
    wait.inputs["SECONDS"].value = "1";
    Block forever = block("forever", "control_forever");
    forever.statements["SUBSTACK"] = {on, wait};
    project.scripts.push_back({100, 50, {block("hat", "events_when_program_starts"), forever}});
    return project;
}

LayoutContext context(const Project& project, s2a::Language language = s2a::Language::English) {
    return {BlockCatalog::builtin(), project, nullptr, language,
            [](const std::string& text) { return 7.0 * static_cast<double>(text.size()); }};
}

const BlockGeometry& byId(const std::vector<BlockGeometry>& blocks, const char* id) {
    for (const auto& geometry : blocks) {
        if (geometry.block->id == id) {
            return geometry;
        }
    }
    throw std::runtime_error(std::string("no block ") + id);
}

TEST(Layout, StacksBlocksAndNestsThemInMouths) {
    const Project project = blink();
    const auto blocks = layoutProject(context(project));
    ASSERT_EQ(blocks.size(), 4U);
    const auto& hat = byId(blocks, "hat");
    const auto& forever = byId(blocks, "forever");
    const auto& on = byId(blocks, "on");
    const auto& wait = byId(blocks, "wait");
    EXPECT_DOUBLE_EQ(hat.bounds.x, 100);
    EXPECT_DOUBLE_EQ(hat.bounds.y, 50);
    EXPECT_DOUBLE_EQ(forever.bounds.y, hat.bounds.bottom());
    ASSERT_EQ(forever.mouths.size(), 1U);
    EXPECT_DOUBLE_EQ(on.bounds.x, forever.bounds.x + metrics::kIndent);
    EXPECT_DOUBLE_EQ(on.bounds.y, forever.mouths[0].rect.y);
    EXPECT_DOUBLE_EQ(wait.bounds.y, on.bounds.bottom());
    EXPECT_DOUBLE_EQ(forever.mouths[0].rect.h, on.bounds.h + wait.bounds.h);
    EXPECT_DOUBLE_EQ(forever.bounds.bottom(), wait.bounds.bottom() + metrics::kArm);
    EXPECT_TRUE(hat.firstInStack);
    EXPECT_TRUE(hat.hasNext);
    EXPECT_TRUE(on.firstInStack);
    EXPECT_FALSE(wait.hasNext);
    EXPECT_TRUE(forever.terminal);
}

TEST(Layout, ShowsFieldsInTheUserLanguage) {
    const Project project = blink();
    const auto blocks = layoutProject(context(project, s2a::Language::French));
    const auto& on = byId(blocks, "on");
    const auto field =
        std::find_if(on.parts.begin(), on.parts.end(), [](const Part& part) { return part.kind == PartKind::Field; });
    ASSERT_NE(field, on.parts.end());
    EXPECT_EQ(field->text, "allumer");
    EXPECT_EQ(field->name, "STATE");
}

TEST(Layout, EmptyMouthsKeepRoomToDropBlocks) {
    Project project;
    Block repeat = block("repeat", "control_repeat");
    repeat.inputs["TIMES"].value = "10";
    project.scripts.push_back({0, 0, {repeat}});
    const auto blocks = layoutProject(context(project));
    ASSERT_EQ(blocks.size(), 1U);
    ASSERT_EQ(blocks[0].mouths.size(), 1U);
    EXPECT_DOUBLE_EQ(blocks[0].mouths[0].rect.h, metrics::kMinSlot);
    EXPECT_DOUBLE_EQ(blocks[0].bounds.h, metrics::kRowHeight + metrics::kMinSlot + metrics::kArm);
    // The empty mouth is background: clicking there selects nothing.
    const Rect mouth = blocks[0].mouths[0].rect;
    EXPECT_EQ(hitBlock(blocks, {mouth.x + 10, mouth.y + 5}), -1);
    EXPECT_EQ(hitBlock(blocks, {5, 5}), 0);
}

TEST(Layout, PlacesReportersInsideTheirInput) {
    Project project;
    Block sum = block("sum", "operators_add");
    sum.inputs["NUM1"].value = "1";
    sum.inputs["NUM2"].value = "2";
    Block wait = block("wait", "control_wait");
    wait.inputs["SECONDS"].block = sum;
    project.scripts.push_back({0, 0, {wait}});
    const auto blocks = layoutProject(context(project));
    ASSERT_EQ(blocks.size(), 2U);
    const auto& outer = byId(blocks, "wait");
    const auto& inner = byId(blocks, "sum");
    const auto input = std::find_if(outer.parts.begin(), outer.parts.end(),
                                    [](const Part& part) { return part.name == "SECONDS"; });
    ASSERT_NE(input, outer.parts.end());
    EXPECT_TRUE(input->hasBlock);
    EXPECT_DOUBLE_EQ(inner.bounds.x, input->rect.x);
    EXPECT_DOUBLE_EQ(inner.bounds.w, input->rect.w);
    EXPECT_GE(outer.bounds.h, inner.bounds.h);
    // Clicking the reporter selects it, not the block holding it.
    EXPECT_EQ(hitBlock(blocks, {inner.bounds.x + 2, inner.bounds.y + inner.bounds.h / 2}), 1);
}

TEST(Layout, NamesVariablesAndKeepsUnknownBlocks) {
    Project project;
    project.variables.push_back({"v1", "score", 0});
    Block set = block("set", "variables_set");
    set.fields["VARIABLE"] = "v1";
    project.scripts.push_back({0, 0, {set, block("strange", "from_the_future")}});
    const auto blocks = layoutProject(context(project));
    ASSERT_EQ(blocks.size(), 2U);
    EXPECT_EQ(blocks[0].parts[1].text, "score");
    EXPECT_EQ(blocks[1].definition, nullptr);
    EXPECT_EQ(blocks[1].parts[0].text, "from_the_future");
}

} // namespace
