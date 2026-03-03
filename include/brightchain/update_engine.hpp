#pragma once

#include <brightchain/document.hpp>
#include <nlohmann/json.hpp>
#include <string>

namespace brightchain::db {

/**
 * Stateless update engine for applying MongoDB-style update operators
 * to JSON documents. Returns modified copies (copy-on-write semantics).
 *
 * Supported operators:
 *   Field:  $set, $unset, $inc, $mul, $min, $max, $rename, $currentDate
 *   Array:  $push, $pull, $addToSet, $pop
 */
class UpdateEngine {
public:
    /**
     * Apply update operators to a document. Returns a modified copy;
     * the original document is not mutated.
     *
     * @param doc    The source document.
     * @param update An object whose keys are update operators (e.g. "$set").
     * @return A new document with the updates applied.
     */
    static Document applyUpdate(const Document& doc, const Document& update);

private:
    /// Set fields to specified values.
    static void applySet(Document& doc, const nlohmann::json& fields);

    /// Remove specified fields.
    static void applyUnset(Document& doc, const nlohmann::json& fields);

    /// Increment numeric fields by specified amounts.
    static void applyInc(Document& doc, const nlohmann::json& fields);

    /// Append values to array fields.
    static void applyPush(Document& doc, const nlohmann::json& fields);

    /// Remove all matching values from array fields.
    static void applyPull(Document& doc, const nlohmann::json& fields);

    /// Add values to array fields only if not already present.
    static void applyAddToSet(Document& doc, const nlohmann::json& fields);

    /// Update fields to the lesser of current and specified values.
    static void applyMin(Document& doc, const nlohmann::json& fields);

    /// Update fields to the greater of current and specified values.
    static void applyMax(Document& doc, const nlohmann::json& fields);

    /// Rename fields.
    static void applyRename(Document& doc, const nlohmann::json& fields);

    /// Set fields to the current date/time as ISO 8601 string.
    static void applyCurrentDate(Document& doc, const nlohmann::json& fields);

    /// Multiply numeric fields by specified factors.
    static void applyMul(Document& doc, const nlohmann::json& fields);

    /// Remove the first or last element of array fields.
    static void applyPop(Document& doc, const nlohmann::json& fields);
};

} // namespace brightchain::db
