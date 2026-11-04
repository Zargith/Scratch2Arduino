#include "s2a/blocks.hpp"
#include "s2a/resources.hpp"

#include <gtest/gtest.h>
#include <string>

namespace {

using s2a::ArgKind;
using s2a::BlockCatalog;
using s2a::BlockShape;
using s2a::Language;

TEST(BuiltinCatalog, IsValid) {
    std::vector<std::string_view> documents;
    // builtin() hides errors (it must never crash the app): re-parse to see them.
    for (const auto& resource : s2a::resources::list("blocks/catalog/")) {
        documents.push_back(resource.data);
    }
    ASSERT_FALSE(documents.empty());
    const auto result = BlockCatalog::fromJson(documents);
    ASSERT_TRUE(result.ok()) << result.error().detail;
}

TEST(BuiltinCatalog, CategoriesAreSortedByToolboxOrder) {
    const auto& categories = BlockCatalog::builtin().categories();
    std::vector<std::string> ids;
    for (const auto& category : categories) {
        ids.push_back(category.id);
    }
    EXPECT_EQ(ids, (std::vector<std::string>{"events", "control", "pins", "operators", "variables"}));
}

TEST(BuiltinCatalog, ContainsTheBlinkBlocks) {
    const auto& catalog = BlockCatalog::builtin();
    for (const char* type : {"events_when_program_starts", "control_forever", "control_wait",
                             "pins_digital_write", "pins_builtin_led"}) {
        EXPECT_NE(catalog.find(type), nullptr) << type;
    }
    EXPECT_EQ(catalog.find("does_not_exist"), nullptr);
}

TEST(BuiltinCatalog, DescribesBlocksCompletely) {
    const auto* write = BlockCatalog::builtin().find("pins_digital_write");
    ASSERT_NE(write, nullptr);
    EXPECT_EQ(write->category, "pins");
    EXPECT_EQ(write->shape, BlockShape::Statement);
    EXPECT_EQ(write->message.get(Language::French), "mettre la broche %1 à l'état %2");
    EXPECT_EQ(write->message.get(Language::English), "set pin %1 to %2");
    const auto* pin = write->findArg("PIN");
    ASSERT_NE(pin, nullptr);
    EXPECT_EQ(pin->kind, ArgKind::Pin);
    EXPECT_EQ(pin->pinCapability, s2a::PinCapability::Digital);
    EXPECT_EQ(pin->defaultValue, "13");
    const auto* state = write->findArg("STATE");
    ASSERT_NE(state, nullptr);
    ASSERT_EQ(state->options.size(), 2U);
    EXPECT_EQ(state->options[0].label.get(Language::French), "HAUT");

    const auto* forever = BlockCatalog::builtin().find("control_forever");
    ASSERT_NE(forever, nullptr);
    EXPECT_EQ(forever->shape, BlockShape::CBlock);
    EXPECT_TRUE(forever->terminal);

    const auto* analogWrite = BlockCatalog::builtin().find("pins_analog_write");
    ASSERT_NE(analogWrite, nullptr);
    EXPECT_EQ(analogWrite->findArg("VALUE")->max, 255.0);
}

TEST(BuiltinCatalog, BlocksInCategoryKeepFileOrder) {
    const auto blocks = BlockCatalog::builtin().blocksIn("events");
    ASSERT_EQ(blocks.size(), 1U);
    EXPECT_EQ(blocks[0]->type, "events_when_program_starts");
    EXPECT_TRUE(BlockCatalog::builtin().blocksIn("nope").empty());
}

// --- Validation -----------------------------------------------------------------------

constexpr std::string_view kValidCatalog = R"j({
  "category": {"id": "test", "colour": "#123456", "name": {"fr": "Test", "en": "Test"}, "order": 1},
  "blocks": [
    {"type": "test_block", "shape": "statement",
     "message": {"fr": "faire %1 avec %2", "en": "do %2 with %1"},
     "tooltip": {"fr": "infobulle", "en": "tooltip"},
     "args": [{"name": "A", "kind": "number", "default": 1, "min": 0, "max": 9},
              {"name": "B", "kind": "dropdown", "default": "X",
               "options": [{"value": "X", "label": {"fr": "x", "en": "x"}}]}]}
  ]
})j";

void expectRejected(std::string_view from, std::string_view to, std::string_view expectedIssue) {
    std::string document{kValidCatalog};
    const auto pos = document.find(from);
    ASSERT_NE(pos, std::string::npos) << from;
    document.replace(pos, from.size(), to);
    const auto result = BlockCatalog::fromJson({document});
    ASSERT_FALSE(result.ok()) << "accepted: " << to;
    EXPECT_EQ(result.error().code, s2a::ErrorCode::InvalidCatalog);
    EXPECT_NE(result.error().detail.find(expectedIssue), std::string::npos) << result.error().detail;
}

TEST(CatalogValidation, AcceptsAValidCatalogWithReorderedPlaceholders) {
    const auto result = BlockCatalog::fromJson({kValidCatalog});
    ASSERT_TRUE(result.ok()) << result.error().detail;
    EXPECT_EQ(result.value().find("test_block")->args.size(), 2U);
}

TEST(CatalogValidation, ChecksPlaceholders) {
    expectRejected(R"("en": "do %2 with %1")", R"("en": "do %1 with %1")", "%2 must appear exactly once");
    expectRejected(R"("en": "do %2 with %1")", R"("en": "do %2 with %3")", "%3 has no matching argument");
    expectRejected(R"("en": "do %2 with %1")", R"("en": "do %2 with %")", "must be followed by an argument number");
    // Must be reported, not thrown (std::stoul overflow).
    expectRejected(R"("en": "do %2 with %1")", R"("en": "do %2 with %1 %99999999999999999999999999999")",
                   "has no matching argument");
}

TEST(CatalogValidation, ChecksIdentifiersAndColours) {
    expectRejected(R"("type": "test_block")", R"("type": "other_block")", "must start with its category id");
    expectRejected(R"("type": "test_block")", R"("type": "test Block")", "lower_snake_case");
    expectRejected(R"("colour": "#123456")", R"("colour": "red")", "#RRGGBB");
    expectRejected(R"("name": "A")", R"("name": "a")", "UPPER_CASE");
}

TEST(CatalogValidation, ChecksArguments) {
    expectRejected(R"("kind": "number")", R"("kind": "integer")", "unknown argument kind");
    expectRejected(R"("min": 0, "max": 9)", R"("min": 10, "max": 9)", "'min' is greater than 'max'");
    expectRejected(R"("default": "X")", R"("default": "Y")", "not one of the options");
    expectRejected(R"("name": "B")", R"("name": "A")", "defined twice");
    expectRejected(R"("kind": "number", "default": 1, "min": 0, "max": 9})",
                   R"("kind": "pin"})", "need a 'capability'");
    expectRejected(R"("kind": "number", "default": 1, "min": 0, "max": 9})",
                   R"("kind": "boolean", "default": 1})", "cannot have a default value");
}

TEST(CatalogValidation, ChecksShapes) {
    expectRejected(R"("shape": "statement")", R"("shape": "c")", "needs at least one statement argument");
    expectRejected(R"("shape": "statement")", R"("shape": "triangle")", "unknown shape");
    expectRejected(R"("shape": "statement")", R"("shape": "reporter", "terminal": true)", "cannot be terminal");
    expectRejected(R"("kind": "number", "default": 1, "min": 0, "max": 9})", R"("kind": "statement"})",
                   "only 'c' blocks can have statement arguments");
}

TEST(CatalogValidation, ChecksLocalizedTexts) {
    expectRejected(R"("tooltip": {"fr": "infobulle", "en": "tooltip"})", R"("tooltip": {"fr": "infobulle"})",
                   "needs non-empty 'fr' and 'en'");
}

TEST(CatalogValidation, RejectsDuplicatesAcrossFiles) {
    const auto result = BlockCatalog::fromJson({kValidCatalog, kValidCatalog});
    ASSERT_FALSE(result.ok());
    EXPECT_NE(result.error().detail.find("category 'test' is defined twice"), std::string::npos);
    EXPECT_NE(result.error().detail.find("block type is defined twice"), std::string::npos);
}

TEST(CatalogValidation, RejectsMalformedDocuments) {
    EXPECT_FALSE(BlockCatalog::fromJson({"[1, 2]"}).ok());
    EXPECT_FALSE(BlockCatalog::fromJson({"{"}).ok());
    EXPECT_FALSE(BlockCatalog::fromJson({R"({"blocks": []})"}).ok());
}

} // namespace
