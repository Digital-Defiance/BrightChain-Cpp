#include <gtest/gtest.h>
#include "brightchain/query_engine.hpp"

using namespace brightchain::db;
using json = nlohmann::json;

// ===== Exact-match filters =====

TEST(QueryEngineTest, EmptyFilterMatchesAll) {
    Document doc = {{"name", "Alice"}, {"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {}));
}

TEST(QueryEngineTest, ExactMatchString) {
    Document doc = {{"name", "Alice"}, {"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"name", "Alice"}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"name", "Bob"}}));
}

TEST(QueryEngineTest, ExactMatchNumber) {
    Document doc = {{"name", "Alice"}, {"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"age", 30}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", 25}}));
}

TEST(QueryEngineTest, ExactMatchBoolean) {
    Document doc = {{"active", true}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"active", true}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"active", false}}));
}

TEST(QueryEngineTest, ExactMatchNull) {
    Document doc = {{"value", nullptr}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"value", nullptr}}));
}

TEST(QueryEngineTest, ExactMatchMultipleFields) {
    Document doc = {{"name", "Alice"}, {"age", 30}, {"active", true}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"name", "Alice"}, {"age", 30}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"name", "Alice"}, {"age", 25}}));
}

TEST(QueryEngineTest, MissingFieldDoesNotMatch) {
    Document doc = {{"name", "Alice"}};
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", 30}}));
}

// ===== Comparison operators =====

TEST(QueryEngineTest, EqOperator) {
    Document doc = {{"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"age", {{"$eq", 30}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", {{"$eq", 25}}}}));
}

TEST(QueryEngineTest, NeOperator) {
    Document doc = {{"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"age", {{"$ne", 25}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", {{"$ne", 30}}}}));
}

TEST(QueryEngineTest, GtOperator) {
    Document doc = {{"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"age", {{"$gt", 25}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", {{"$gt", 30}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", {{"$gt", 35}}}}));
}

TEST(QueryEngineTest, GteOperator) {
    Document doc = {{"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"age", {{"$gte", 25}}}}));
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"age", {{"$gte", 30}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", {{"$gte", 35}}}}));
}

TEST(QueryEngineTest, LtOperator) {
    Document doc = {{"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"age", {{"$lt", 35}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", {{"$lt", 30}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", {{"$lt", 25}}}}));
}

TEST(QueryEngineTest, LteOperator) {
    Document doc = {{"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"age", {{"$lte", 35}}}}));
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"age", {{"$lte", 30}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"age", {{"$lte", 25}}}}));
}

TEST(QueryEngineTest, ComparisonOnStrings) {
    Document doc = {{"name", "banana"}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"name", {{"$gt", "apple"}}}}));
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"name", {{"$lt", "cherry"}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"name", {{"$gt", "cherry"}}}}));
}

TEST(QueryEngineTest, MultipleOperatorsOnSameField) {
    Document doc = {{"age", 30}};
    // age >= 25 AND age <= 35
    Document filter = {{"age", {{"$gte", 25}, {"$lte", 35}}}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, filter));

    // age >= 31 AND age <= 35 — should fail
    Document filter2 = {{"age", {{"$gte", 31}, {"$lte", 35}}}};
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, filter2));
}

// ===== Set operators =====

TEST(QueryEngineTest, InOperator) {
    Document doc = {{"status", "active"}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"status", {{"$in", json::array({"active", "pending"})}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"status", {{"$in", json::array({"closed", "pending"})}}}}));
}

TEST(QueryEngineTest, InOperatorWithNumbers) {
    Document doc = {{"code", 42}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"code", {{"$in", json::array({10, 42, 99})}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"code", {{"$in", json::array({10, 20, 99})}}}}));
}

TEST(QueryEngineTest, NinOperator) {
    Document doc = {{"status", "active"}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"status", {{"$nin", json::array({"closed", "pending"})}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"status", {{"$nin", json::array({"active", "pending"})}}}}));
}

// ===== $exists operator =====

TEST(QueryEngineTest, ExistsTrue) {
    Document doc = {{"name", "Alice"}, {"age", 30}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"name", {{"$exists", true}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"email", {{"$exists", true}}}}));
}

TEST(QueryEngineTest, ExistsFalse) {
    Document doc = {{"name", "Alice"}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"email", {{"$exists", false}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"name", {{"$exists", false}}}}));
}

TEST(QueryEngineTest, ExistsWithOtherOperators) {
    Document doc = {{"age", 30}};
    // Field exists AND age > 25
    Document filter = {{"age", {{"$exists", true}, {"$gt", 25}}}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, filter));

    // Field exists AND age > 35 — should fail
    Document filter2 = {{"age", {{"$exists", true}, {"$gt", 35}}}};
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, filter2));
}

// ===== Logical operators =====

TEST(QueryEngineTest, AndOperator) {
    Document doc = {{"name", "Alice"}, {"age", 30}};
    Document filter = {{"$and", json::array({
        {{"name", "Alice"}},
        {{"age", {{"$gte", 25}}}}
    })}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, filter));

    Document filter2 = {{"$and", json::array({
        {{"name", "Alice"}},
        {{"age", {{"$gte", 35}}}}
    })}};
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, filter2));
}

TEST(QueryEngineTest, OrOperator) {
    Document doc = {{"name", "Alice"}, {"age", 30}};
    Document filter = {{"$or", json::array({
        {{"name", "Bob"}},
        {{"age", 30}}
    })}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, filter));

    Document filter2 = {{"$or", json::array({
        {{"name", "Bob"}},
        {{"age", 25}}
    })}};
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, filter2));
}

TEST(QueryEngineTest, OrWithEmptyArray) {
    Document doc = {{"name", "Alice"}};
    // $or with empty array — no conditions to satisfy, should be false
    Document filter = {{"$or", json::array()}};
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, filter));
}

TEST(QueryEngineTest, AndWithEmptyArray) {
    Document doc = {{"name", "Alice"}};
    // $and with empty array — all (zero) conditions satisfied, should be true
    Document filter = {{"$and", json::array()}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, filter));
}

TEST(QueryEngineTest, NestedLogicalOperators) {
    Document doc = {{"name", "Alice"}, {"age", 30}, {"city", "NYC"}};
    // ($or: [name=Bob, age>=25]) AND city=NYC
    Document filter = {
        {"$and", json::array({
            {{"$or", json::array({
                {{"name", "Bob"}},
                {{"age", {{"$gte", 25}}}}
            })}},
            {{"city", "NYC"}}
        })}
    };
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, filter));
}

// ===== Cross-type comparison behavior =====

TEST(QueryEngineTest, CrossTypeComparisonOrder) {
    // null < bool < number < string < array < object
    Document docNull = {{"v", nullptr}};
    Document docBool = {{"v", true}};
    Document docNum = {{"v", 42}};
    Document docStr = {{"v", "hello"}};
    Document docArr = {{"v", json::array({1, 2})}};
    Document docObj = {{"v", {{"a", 1}}}};

    // null < number
    EXPECT_TRUE(QueryEngine::matchesFilter(docNull, {{"v", {{"$lt", 42}}}}));
    // bool < number
    EXPECT_TRUE(QueryEngine::matchesFilter(docBool, {{"v", {{"$lt", 42}}}}));
    // number < string
    EXPECT_TRUE(QueryEngine::matchesFilter(docNum, {{"v", {{"$lt", "hello"}}}}));
    // string < array
    EXPECT_TRUE(QueryEngine::matchesFilter(docStr, {{"v", {{"$lt", json::array({1})}}}}));
    // array < object
    EXPECT_TRUE(QueryEngine::matchesFilter(docArr, {{"v", {{"$lt", {{"a", 1}}}}}}));

    // Cross-type $ne should be true
    EXPECT_TRUE(QueryEngine::matchesFilter(docNum, {{"v", {{"$ne", "hello"}}}}));
}

TEST(QueryEngineTest, CrossTypeEqNeverMatches) {
    Document doc = {{"v", 42}};
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"v", {{"$eq", "42"}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"v", {{"$eq", true}}}}));
}

// ===== Nested field matching (dot notation) =====

TEST(QueryEngineTest, NestedFieldExactMatch) {
    Document doc = {{"address", {{"city", "NYC"}, {"zip", "10001"}}}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"address.city", "NYC"}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"address.city", "LA"}}));
}

TEST(QueryEngineTest, NestedFieldWithOperator) {
    Document doc = {{"stats", {{"score", 85}}}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"stats.score", {{"$gte", 80}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"stats.score", {{"$gte", 90}}}}));
}

TEST(QueryEngineTest, DeeplyNestedField) {
    Document doc = {{"a", {{"b", {{"c", 42}}}}}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"a.b.c", 42}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"a.b.c", 99}}));
}

TEST(QueryEngineTest, NestedFieldMissing) {
    Document doc = {{"address", {{"city", "NYC"}}}};
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"address.state", "NY"}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"foo.bar", "baz"}}));
}

TEST(QueryEngineTest, NestedFieldExistsOperator) {
    Document doc = {{"address", {{"city", "NYC"}}}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"address.city", {{"$exists", true}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"address.state", {{"$exists", true}}}}));
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"address.state", {{"$exists", false}}}}));
}

// ===== Edge cases =====

TEST(QueryEngineTest, ExactMatchArray) {
    Document doc = {{"tags", json::array({"a", "b", "c"})}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"tags", json::array({"a", "b", "c"})}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"tags", json::array({"a", "b"})}}));
}

TEST(QueryEngineTest, ExactMatchObject) {
    Document doc = {{"meta", {{"x", 1}, {"y", 2}}}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"meta", {{"x", 1}, {"y", 2}}}}));
}

TEST(QueryEngineTest, BoolComparison) {
    Document doc = {{"flag", false}};
    EXPECT_TRUE(QueryEngine::matchesFilter(doc, {{"flag", {{"$lt", true}}}}));
    EXPECT_FALSE(QueryEngine::matchesFilter(doc, {{"flag", {{"$gt", true}}}}));
}
