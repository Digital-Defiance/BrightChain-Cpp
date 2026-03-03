#include <gtest/gtest.h>
#include "brightchain/update_engine.hpp"

using namespace brightchain::db;
using json = nlohmann::json;

// ===== $set =====

TEST(UpdateEngineTest, SetAddsNewField) {
    Document doc = {{"_id", "1"}, {"name", "Alice"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$set", {{"age", 30}}}});
    EXPECT_EQ(result["age"], 30);
    EXPECT_EQ(result["name"], "Alice");
}

TEST(UpdateEngineTest, SetOverwritesExistingField) {
    Document doc = {{"_id", "1"}, {"name", "Alice"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$set", {{"name", "Bob"}}}});
    EXPECT_EQ(result["name"], "Bob");
}

// ===== $unset =====

TEST(UpdateEngineTest, UnsetRemovesField) {
    Document doc = {{"_id", "1"}, {"name", "Alice"}, {"age", 30}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$unset", {{"age", ""}}}});
    EXPECT_FALSE(result.contains("age"));
    EXPECT_EQ(result["name"], "Alice");
}

TEST(UpdateEngineTest, UnsetMissingFieldIsNoop) {
    Document doc = {{"_id", "1"}, {"name", "Alice"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$unset", {{"missing", ""}}}});
    EXPECT_EQ(result, doc);
}

// ===== $inc =====

TEST(UpdateEngineTest, IncIncrementsExistingNumber) {
    Document doc = {{"_id", "1"}, {"count", 10}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$inc", {{"count", 5}}}});
    EXPECT_EQ(result["count"], 15);
}

TEST(UpdateEngineTest, IncCreatesFieldIfMissing) {
    Document doc = {{"_id", "1"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$inc", {{"count", 3}}}});
    EXPECT_EQ(result["count"], 3);
}

TEST(UpdateEngineTest, IncWithNegativeValue) {
    Document doc = {{"_id", "1"}, {"count", 10}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$inc", {{"count", -3}}}});
    EXPECT_EQ(result["count"], 7);
}

// ===== $mul =====

TEST(UpdateEngineTest, MulMultipliesExistingNumber) {
    Document doc = {{"_id", "1"}, {"price", 10.0}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$mul", {{"price", 2.5}}}});
    EXPECT_DOUBLE_EQ(result["price"].get<double>(), 25.0);
}

TEST(UpdateEngineTest, MulMissingFieldSetsToZero) {
    Document doc = {{"_id", "1"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$mul", {{"qty", 5}}}});
    EXPECT_EQ(result["qty"], 0);
}

// ===== $min =====

TEST(UpdateEngineTest, MinUpdatesWhenSmaller) {
    Document doc = {{"_id", "1"}, {"score", 80}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$min", {{"score", 60}}}});
    EXPECT_EQ(result["score"], 60);
}

TEST(UpdateEngineTest, MinNoChangeWhenLarger) {
    Document doc = {{"_id", "1"}, {"score", 80}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$min", {{"score", 90}}}});
    EXPECT_EQ(result["score"], 80);
}

TEST(UpdateEngineTest, MinCreatesFieldIfMissing) {
    Document doc = {{"_id", "1"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$min", {{"score", 50}}}});
    EXPECT_EQ(result["score"], 50);
}

// ===== $max =====

TEST(UpdateEngineTest, MaxUpdatesWhenLarger) {
    Document doc = {{"_id", "1"}, {"score", 80}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$max", {{"score", 95}}}});
    EXPECT_EQ(result["score"], 95);
}

TEST(UpdateEngineTest, MaxNoChangeWhenSmaller) {
    Document doc = {{"_id", "1"}, {"score", 80}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$max", {{"score", 70}}}});
    EXPECT_EQ(result["score"], 80);
}

TEST(UpdateEngineTest, MaxCreatesFieldIfMissing) {
    Document doc = {{"_id", "1"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$max", {{"score", 50}}}});
    EXPECT_EQ(result["score"], 50);
}

// ===== $rename =====

TEST(UpdateEngineTest, RenameMovesField) {
    Document doc = {{"_id", "1"}, {"old_name", "Alice"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$rename", {{"old_name", "new_name"}}}});
    EXPECT_FALSE(result.contains("old_name"));
    EXPECT_EQ(result["new_name"], "Alice");
}

TEST(UpdateEngineTest, RenameMissingFieldIsNoop) {
    Document doc = {{"_id", "1"}, {"name", "Alice"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$rename", {{"missing", "other"}}}});
    EXPECT_EQ(result, doc);
}

// ===== $currentDate =====

TEST(UpdateEngineTest, CurrentDateSetsTimestamp) {
    Document doc = {{"_id", "1"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$currentDate", {{"updatedAt", true}}}});
    ASSERT_TRUE(result.contains("updatedAt"));
    // Should be an ISO 8601 string ending with Z
    std::string ts = result["updatedAt"].get<std::string>();
    EXPECT_FALSE(ts.empty());
    EXPECT_EQ(ts.back(), 'Z');
    // Basic format check: YYYY-MM-DDTHH:MM:SSZ = 20 chars
    EXPECT_EQ(ts.size(), 20u);
}

// ===== $push =====

TEST(UpdateEngineTest, PushAppendsToArray) {
    Document doc = {{"_id", "1"}, {"tags", json::array({"a", "b"})}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$push", {{"tags", "c"}}}});
    ASSERT_EQ(result["tags"].size(), 3u);
    EXPECT_EQ(result["tags"][2], "c");
}

TEST(UpdateEngineTest, PushCreatesArrayIfMissing) {
    Document doc = {{"_id", "1"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$push", {{"tags", "a"}}}});
    ASSERT_TRUE(result["tags"].is_array());
    ASSERT_EQ(result["tags"].size(), 1u);
    EXPECT_EQ(result["tags"][0], "a");
}

// ===== $pull =====

TEST(UpdateEngineTest, PullRemovesMatchingValues) {
    Document doc = {{"_id", "1"}, {"tags", json::array({"a", "b", "a", "c"})}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$pull", {{"tags", "a"}}}});
    ASSERT_EQ(result["tags"].size(), 2u);
    EXPECT_EQ(result["tags"][0], "b");
    EXPECT_EQ(result["tags"][1], "c");
}

TEST(UpdateEngineTest, PullNoMatchIsNoop) {
    Document doc = {{"_id", "1"}, {"tags", json::array({"a", "b"})}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$pull", {{"tags", "z"}}}});
    EXPECT_EQ(result["tags"].size(), 2u);
}

// ===== $addToSet =====

TEST(UpdateEngineTest, AddToSetAddsIfNotPresent) {
    Document doc = {{"_id", "1"}, {"tags", json::array({"a", "b"})}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$addToSet", {{"tags", "c"}}}});
    ASSERT_EQ(result["tags"].size(), 3u);
    EXPECT_EQ(result["tags"][2], "c");
}

TEST(UpdateEngineTest, AddToSetSkipsIfAlreadyPresent) {
    Document doc = {{"_id", "1"}, {"tags", json::array({"a", "b"})}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$addToSet", {{"tags", "a"}}}});
    EXPECT_EQ(result["tags"].size(), 2u);
}

TEST(UpdateEngineTest, AddToSetCreatesArrayIfMissing) {
    Document doc = {{"_id", "1"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$addToSet", {{"tags", "x"}}}});
    ASSERT_TRUE(result["tags"].is_array());
    ASSERT_EQ(result["tags"].size(), 1u);
    EXPECT_EQ(result["tags"][0], "x");
}

// ===== $pop =====

TEST(UpdateEngineTest, PopLastElement) {
    Document doc = {{"_id", "1"}, {"arr", json::array({1, 2, 3})}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$pop", {{"arr", 1}}}});
    ASSERT_EQ(result["arr"].size(), 2u);
    EXPECT_EQ(result["arr"][0], 1);
    EXPECT_EQ(result["arr"][1], 2);
}

TEST(UpdateEngineTest, PopFirstElement) {
    Document doc = {{"_id", "1"}, {"arr", json::array({1, 2, 3})}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$pop", {{"arr", -1}}}});
    ASSERT_EQ(result["arr"].size(), 2u);
    EXPECT_EQ(result["arr"][0], 2);
    EXPECT_EQ(result["arr"][1], 3);
}

TEST(UpdateEngineTest, PopEmptyArrayIsNoop) {
    Document doc = {{"_id", "1"}, {"arr", json::array()}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$pop", {{"arr", 1}}}});
    EXPECT_TRUE(result["arr"].empty());
}

// ===== Combined update with multiple operators =====

TEST(UpdateEngineTest, CombinedMultipleOperators) {
    Document doc = {
        {"_id", "1"},
        {"name", "Alice"},
        {"age", 25},
        {"tags", json::array({"dev"})},
        {"obsolete", true}
    };
    Document update = {
        {"$set", {{"name", "Bob"}}},
        {"$inc", {{"age", 5}}},
        {"$push", {{"tags", "lead"}}},
        {"$unset", {{"obsolete", ""}}}
    };
    auto result = UpdateEngine::applyUpdate(doc, update);
    EXPECT_EQ(result["name"], "Bob");
    EXPECT_EQ(result["age"], 30);
    ASSERT_EQ(result["tags"].size(), 2u);
    EXPECT_EQ(result["tags"][1], "lead");
    EXPECT_FALSE(result.contains("obsolete"));
    EXPECT_EQ(result["_id"], "1");
}

// ===== Copy-on-write: original document is not mutated =====

TEST(UpdateEngineTest, OriginalDocumentNotMutated) {
    Document original = {{"_id", "1"}, {"name", "Alice"}, {"count", 10}};
    Document originalCopy = original; // snapshot

    auto result = UpdateEngine::applyUpdate(original, {
        {"$set", {{"name", "Bob"}}},
        {"$inc", {{"count", 5}}},
        {"$push", {{"tags", "new"}}}
    });

    // Original must be unchanged
    EXPECT_EQ(original, originalCopy);
    // Result must differ
    EXPECT_NE(result, original);
    EXPECT_EQ(result["name"], "Bob");
    EXPECT_EQ(original["name"], "Alice");
}

// ===== Edge cases =====

TEST(UpdateEngineTest, EmptyUpdateReturnsDocCopy) {
    Document doc = {{"_id", "1"}, {"name", "Alice"}};
    auto result = UpdateEngine::applyUpdate(doc, json::object());
    EXPECT_EQ(result, doc);
}

TEST(UpdateEngineTest, NonObjectUpdateReturnsDocCopy) {
    Document doc = {{"_id", "1"}, {"name", "Alice"}};
    auto result = UpdateEngine::applyUpdate(doc, "not an object");
    EXPECT_EQ(result, doc);
}

TEST(UpdateEngineTest, UnknownOperatorIsIgnored) {
    Document doc = {{"_id", "1"}, {"name", "Alice"}};
    auto result = UpdateEngine::applyUpdate(doc, {{"$unknown", {{"name", "Bob"}}}});
    EXPECT_EQ(result["name"], "Alice");
}
