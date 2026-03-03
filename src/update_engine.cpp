#include "brightchain/update_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace brightchain::db {

// ---------------------------------------------------------------------------
// applyUpdate — top-level entry point (copy-on-write)
// ---------------------------------------------------------------------------
Document UpdateEngine::applyUpdate(const Document& doc, const Document& update) {
    Document result = doc; // copy

    if (!update.is_object()) return result;

    for (auto it = update.begin(); it != update.end(); ++it) {
        const auto& op = it.key();
        const auto& fields = it.value();

        if (!fields.is_object()) continue;

        if      (op == "$set")         applySet(result, fields);
        else if (op == "$unset")       applyUnset(result, fields);
        else if (op == "$inc")         applyInc(result, fields);
        else if (op == "$push")        applyPush(result, fields);
        else if (op == "$pull")        applyPull(result, fields);
        else if (op == "$addToSet")    applyAddToSet(result, fields);
        else if (op == "$min")         applyMin(result, fields);
        else if (op == "$max")         applyMax(result, fields);
        else if (op == "$rename")      applyRename(result, fields);
        else if (op == "$currentDate") applyCurrentDate(result, fields);
        else if (op == "$mul")         applyMul(result, fields);
        else if (op == "$pop")         applyPop(result, fields);
    }

    return result;
}

// ---------------------------------------------------------------------------
// $set — set fields to specified values
// ---------------------------------------------------------------------------
void UpdateEngine::applySet(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        doc[it.key()] = it.value();
    }
}

// ---------------------------------------------------------------------------
// $unset — remove specified fields
// ---------------------------------------------------------------------------
void UpdateEngine::applyUnset(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        doc.erase(it.key());
    }
}

// ---------------------------------------------------------------------------
// $inc — increment numeric fields
// ---------------------------------------------------------------------------
void UpdateEngine::applyInc(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto& key = it.key();
        const auto& amount = it.value();
        if (!amount.is_number()) continue;

        if (doc.contains(key) && doc[key].is_number()) {
            doc[key] = doc[key].get<double>() + amount.get<double>();
        } else {
            // Field missing or not numeric — set to the increment value
            doc[key] = amount;
        }
    }
}

// ---------------------------------------------------------------------------
// $mul — multiply numeric fields
// ---------------------------------------------------------------------------
void UpdateEngine::applyMul(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto& key = it.key();
        const auto& factor = it.value();
        if (!factor.is_number()) continue;

        if (doc.contains(key) && doc[key].is_number()) {
            doc[key] = doc[key].get<double>() * factor.get<double>();
        } else {
            // Field missing or not numeric — set to 0 (0 * factor)
            doc[key] = 0;
        }
    }
}

// ---------------------------------------------------------------------------
// $min — update to lesser value
// ---------------------------------------------------------------------------
void UpdateEngine::applyMin(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto& key = it.key();
        const auto& val = it.value();

        if (!doc.contains(key)) {
            doc[key] = val;
        } else if (val < doc[key]) {
            doc[key] = val;
        }
    }
}

// ---------------------------------------------------------------------------
// $max — update to greater value
// ---------------------------------------------------------------------------
void UpdateEngine::applyMax(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto& key = it.key();
        const auto& val = it.value();

        if (!doc.contains(key)) {
            doc[key] = val;
        } else if (val > doc[key]) {
            doc[key] = val;
        }
    }
}

// ---------------------------------------------------------------------------
// $rename — rename fields
// ---------------------------------------------------------------------------
void UpdateEngine::applyRename(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto& oldName = it.key();
        if (!it.value().is_string()) continue;
        const auto& newName = it.value().get_ref<const std::string&>();

        if (doc.contains(oldName)) {
            doc[newName] = std::move(doc[oldName]);
            doc.erase(oldName);
        }
    }
}

// ---------------------------------------------------------------------------
// $currentDate — set fields to current ISO 8601 timestamp
// ---------------------------------------------------------------------------
void UpdateEngine::applyCurrentDate(Document& doc, const nlohmann::json& fields) {
    // Generate ISO 8601 timestamp once for all fields in this operator
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    gmtime_s(&tm_buf, &time_t_now);
#else
    gmtime_r(&time_t_now, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%dT%H:%M:%S") << "Z";
    std::string timestamp = oss.str();

    for (auto it = fields.begin(); it != fields.end(); ++it) {
        // Accept both {field: true} and {field: {$type: "date"}}
        doc[it.key()] = timestamp;
    }
}

// ---------------------------------------------------------------------------
// $push — append values to arrays
// ---------------------------------------------------------------------------
void UpdateEngine::applyPush(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto& key = it.key();

        if (!doc.contains(key)) {
            doc[key] = nlohmann::json::array();
        }
        if (!doc[key].is_array()) continue;

        doc[key].push_back(it.value());
    }
}

// ---------------------------------------------------------------------------
// $pull — remove all matching values from arrays
// ---------------------------------------------------------------------------
void UpdateEngine::applyPull(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto& key = it.key();
        const auto& matchVal = it.value();

        if (!doc.contains(key) || !doc[key].is_array()) continue;

        nlohmann::json newArr = nlohmann::json::array();
        for (const auto& elem : doc[key]) {
            if (elem != matchVal) {
                newArr.push_back(elem);
            }
        }
        doc[key] = std::move(newArr);
    }
}

// ---------------------------------------------------------------------------
// $addToSet — add to array only if not already present
// ---------------------------------------------------------------------------
void UpdateEngine::applyAddToSet(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto& key = it.key();
        const auto& val = it.value();

        if (!doc.contains(key)) {
            doc[key] = nlohmann::json::array();
        }
        if (!doc[key].is_array()) continue;

        bool found = false;
        for (const auto& elem : doc[key]) {
            if (elem == val) {
                found = true;
                break;
            }
        }
        if (!found) {
            doc[key].push_back(val);
        }
    }
}

// ---------------------------------------------------------------------------
// $pop — remove first (-1) or last (1) element from arrays
// ---------------------------------------------------------------------------
void UpdateEngine::applyPop(Document& doc, const nlohmann::json& fields) {
    for (auto it = fields.begin(); it != fields.end(); ++it) {
        const auto& key = it.key();
        const auto& direction = it.value();

        if (!doc.contains(key) || !doc[key].is_array()) continue;
        if (doc[key].empty()) continue;
        if (!direction.is_number()) continue;

        int dir = direction.get<int>();
        if (dir == 1) {
            // Remove last element
            doc[key].erase(doc[key].end() - 1);
        } else if (dir == -1) {
            // Remove first element
            doc[key].erase(doc[key].begin());
        }
    }
}

} // namespace brightchain::db
