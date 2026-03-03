#include "brightchain/query_engine.hpp"
#include <algorithm>
#include <sstream>

namespace brightchain::db {

// ---------------------------------------------------------------------------
// Type rank for MongoDB-style ordering: null < bool < number < string < array < object
// ---------------------------------------------------------------------------
int QueryEngine::typeRank(const nlohmann::json& v) {
    if (v.is_null())                        return 0;
    if (v.is_boolean())                     return 1;
    if (v.is_number())                      return 2;
    if (v.is_string())                      return 3;
    if (v.is_array())                       return 4;
    if (v.is_object())                      return 5;
    return -1; // should not happen
}

// ---------------------------------------------------------------------------
// compareValues — cross-type aware comparison
// ---------------------------------------------------------------------------
int QueryEngine::compareValues(const nlohmann::json& a, const nlohmann::json& b) {
    int ra = typeRank(a);
    int rb = typeRank(b);
    if (ra != rb) return ra - rb;

    // Same type — compare within type
    switch (ra) {
        case 0: // null == null
            return 0;
        case 1: { // bool: false < true
            bool ba = a.get<bool>();
            bool bb = b.get<bool>();
            return (ba == bb) ? 0 : (ba ? 1 : -1);
        }
        case 2: { // number
            double da = a.get<double>();
            double db = b.get<double>();
            if (da < db) return -1;
            if (da > db) return 1;
            return 0;
        }
        case 3: { // string
            const auto& sa = a.get_ref<const std::string&>();
            const auto& sb = b.get_ref<const std::string&>();
            return sa.compare(sb);
        }
        case 4: // array — lexicographic element comparison
        case 5: // object — use nlohmann::json operator< which does ordered comparison
            if (a == b) return 0;
            return (a < b) ? -1 : 1;
        default:
            return 0;
    }
}

// ---------------------------------------------------------------------------
// resolveField — supports dot-notation (e.g. "address.city")
// ---------------------------------------------------------------------------
const nlohmann::json* QueryEngine::resolveField(const Document& doc,
                                                 const std::string& fieldPath) {
    const nlohmann::json* current = &doc;
    std::istringstream stream(fieldPath);
    std::string segment;

    while (std::getline(stream, segment, '.')) {
        if (!current->is_object() || !current->contains(segment)) {
            return nullptr;
        }
        current = &(*current)[segment];
    }
    return current;
}

// ---------------------------------------------------------------------------
// matchesOperator — single comparison/set/existence operator
// ---------------------------------------------------------------------------
bool QueryEngine::matchesOperator(const nlohmann::json& fieldValue,
                                  const std::string& op,
                                  const nlohmann::json& operand) {
    if (op == "$eq") {
        return compareValues(fieldValue, operand) == 0;
    }
    if (op == "$ne") {
        return compareValues(fieldValue, operand) != 0;
    }
    if (op == "$gt") {
        return compareValues(fieldValue, operand) > 0;
    }
    if (op == "$gte") {
        return compareValues(fieldValue, operand) >= 0;
    }
    if (op == "$lt") {
        return compareValues(fieldValue, operand) < 0;
    }
    if (op == "$lte") {
        return compareValues(fieldValue, operand) <= 0;
    }
    if (op == "$in") {
        if (!operand.is_array()) return false;
        for (const auto& item : operand) {
            if (compareValues(fieldValue, item) == 0) return true;
        }
        return false;
    }
    if (op == "$nin") {
        if (!operand.is_array()) return true;
        for (const auto& item : operand) {
            if (compareValues(fieldValue, item) == 0) return false;
        }
        return true;
    }
    if (op == "$exists") {
        // $exists is handled at the matchesFilter level since it checks field presence.
        // If we reach here, the field exists, so $exists:true → true, $exists:false → false.
        bool shouldExist = operand.is_boolean() ? operand.get<bool>() : true;
        return shouldExist;
    }
    // Unknown operator — treat as no match
    return false;
}

// ---------------------------------------------------------------------------
// matchesFieldFilter — exact match or operator object
// ---------------------------------------------------------------------------
bool QueryEngine::matchesFieldFilter(const nlohmann::json& fieldValue,
                                     const nlohmann::json& filterValue) {
    // If the filter value is an object, check if it contains operators (keys starting with '$')
    if (filterValue.is_object()) {
        bool hasOperators = false;
        for (auto it = filterValue.begin(); it != filterValue.end(); ++it) {
            if (!it.key().empty() && it.key()[0] == '$') {
                hasOperators = true;
                break;
            }
        }
        if (hasOperators) {
            // All operator conditions must be satisfied (implicit AND)
            for (auto it = filterValue.begin(); it != filterValue.end(); ++it) {
                if (!matchesOperator(fieldValue, it.key(), it.value())) {
                    return false;
                }
            }
            return true;
        }
    }

    // Exact match
    return compareValues(fieldValue, filterValue) == 0;
}

// ---------------------------------------------------------------------------
// matchesLogicalOperator — $and, $or
// ---------------------------------------------------------------------------
bool QueryEngine::matchesLogicalOperator(const Document& doc,
                                         const std::string& op,
                                         const nlohmann::json& operand) {
    if (!operand.is_array()) return false;

    if (op == "$and") {
        for (const auto& subFilter : operand) {
            if (!matchesFilter(doc, subFilter)) return false;
        }
        return true;
    }
    if (op == "$or") {
        for (const auto& subFilter : operand) {
            if (matchesFilter(doc, subFilter)) return true;
        }
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// matchesFilter — top-level entry point
// ---------------------------------------------------------------------------
bool QueryEngine::matchesFilter(const Document& doc, const Document& filter) {
    // Empty filter matches everything
    if (filter.empty()) return true;
    if (!filter.is_object()) return false;

    for (auto it = filter.begin(); it != filter.end(); ++it) {
        const auto& key = it.key();
        const auto& filterValue = it.value();

        // Logical operators apply to the whole document
        if (key == "$and" || key == "$or") {
            if (!matchesLogicalOperator(doc, key, filterValue)) {
                return false;
            }
            continue;
        }

        // $exists on a missing field
        if (filterValue.is_object() && filterValue.contains("$exists")) {
            const nlohmann::json* fieldPtr = resolveField(doc, key);
            bool exists = (fieldPtr != nullptr);
            bool shouldExist = filterValue["$exists"].is_boolean()
                                   ? filterValue["$exists"].get<bool>()
                                   : true;

            if (exists != shouldExist) return false;

            // If there are other operators besides $exists, evaluate them too
            if (filterValue.size() > 1 && exists) {
                for (auto opIt = filterValue.begin(); opIt != filterValue.end(); ++opIt) {
                    if (opIt.key() == "$exists") continue;
                    if (!matchesOperator(*fieldPtr, opIt.key(), opIt.value())) {
                        return false;
                    }
                }
            }
            continue;
        }

        // Regular field filter — field must exist
        const nlohmann::json* fieldPtr = resolveField(doc, key);
        if (fieldPtr == nullptr) return false;

        if (!matchesFieldFilter(*fieldPtr, filterValue)) {
            return false;
        }
    }

    return true;
}

} // namespace brightchain::db
