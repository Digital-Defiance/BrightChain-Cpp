#pragma once

#include <brightchain/document.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace brightchain::db {

/**
 * Stateless query engine for evaluating MongoDB-style filter expressions
 * against JSON documents. Uses linear scan evaluation.
 *
 * Supported operators:
 *   Comparison: $eq, $ne, $gt, $gte, $lt, $lte
 *   Set:        $in, $nin
 *   Existence:  $exists
 *   Logical:    $and, $or
 *
 * Value comparison order (MongoDB semantics):
 *   null < bool < number < string < array < object
 */
class QueryEngine {
public:
    /**
     * Returns true if the document matches the filter expression.
     * An empty filter matches all documents.
     */
    static bool matchesFilter(const Document& doc, const Document& filter);

private:
    /// Match a single field's value against a filter value (exact or operator object).
    static bool matchesFieldFilter(const nlohmann::json& fieldValue,
                                   const nlohmann::json& filterValue);

    /// Evaluate a single comparison/set/existence operator.
    static bool matchesOperator(const nlohmann::json& fieldValue,
                                const std::string& op,
                                const nlohmann::json& operand);

    /// Evaluate a logical operator ($and, $or) against the full document.
    static bool matchesLogicalOperator(const Document& doc,
                                       const std::string& op,
                                       const nlohmann::json& operand);

    /**
     * Compare two JSON values following MongoDB type ordering:
     *   null(0) < bool(1) < number(2) < string(3) < array(4) < object(5)
     *
     * Returns <0 if a < b, 0 if a == b, >0 if a > b.
     * Cross-type comparisons use the type order rank.
     */
    static int compareValues(const nlohmann::json& a, const nlohmann::json& b);

    /// Return the type-order rank for a JSON value.
    static int typeRank(const nlohmann::json& v);

    /// Resolve a potentially dot-notated field path in a document.
    static const nlohmann::json* resolveField(const Document& doc,
                                              const std::string& fieldPath);
};

} // namespace brightchain::db
