#include "brightchain/v2/filter.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <regex>
#include <set>

namespace brightchain::v2 {

namespace {

using nlohmann::json;
using Value = std::optional<json>;  // nullopt is JavaScript's undefined

bool isNumber(const json& v) { return v.is_number(); }

Value getNested(const json& doc, const std::string& path) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (true) {
    auto dot = path.find('.', start);
    parts.push_back(path.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  Value current = doc;
  for (const auto& part : parts) {
    if (!current || current->is_null()) return std::nullopt;
    if (current->is_array()) {
      char* end = nullptr;
      double idx = std::strtod(part.c_str(), &end);
      if (!part.empty() && end && *end == '\0') {
        auto i = static_cast<std::size_t>(idx);
        if (idx < 0 || idx != std::floor(idx) || i >= current->size()) return std::nullopt;
        current = (*current)[i];
      } else {
        json results = json::array();
        for (const auto& item : *current) {
          if (item.is_object() && item.contains(part)) results.push_back(item.at(part));
        }
        if (results.size() == 1) return results[0];
        if (!results.empty()) return results;
        return std::nullopt;
      }
    } else if (current->is_object()) {
      if (!current->contains(part)) return std::nullopt;
      current = current->at(part);
    } else {
      return std::nullopt;
    }
  }
  return current;
}

std::string jsNumberToString(double d) {
  if (d == std::floor(d) && std::fabs(d) < 1e21) return std::to_string(static_cast<long long>(d));
  char buf[64];
  std::snprintf(buf, sizeof buf, "%.17g", d);
  return buf;
}

std::string toText(const Value& v) {
  if (!v) return "undefined";
  const auto& j = *v;
  if (j.is_null()) return "null";
  if (j.is_boolean()) return j.get<bool>() ? "true" : "false";
  if (j.is_number()) return jsNumberToString(j.get<double>());
  if (j.is_string()) return j.get<std::string>();
  if (j.is_array()) {
    std::string out;
    bool first = true;
    for (const auto& item : j) {
      if (!first) out += ",";
      first = false;
      if (!item.is_null()) out += toText(item);
    }
    return out;
  }
  return "[object Object]";
}

bool nullish(const Value& v) { return !v || v->is_null(); }

int sign(double d) { return d < 0 ? -1 : (d > 0 ? 1 : 0); }

int compareText(const std::string& a, const std::string& b) {
  int c = a.compare(b);
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

int compareValues(const Value& a, const Value& b);

bool deepEquals(const Value& a, const Value& b) {
  if (nullish(a) && nullish(b)) return true;
  if (!a || !b) return false;
  const auto& x = *a;
  const auto& y = *b;
  if (x.is_number() && y.is_number()) return x.get<double>() == y.get<double>();
  if (x.type() == json::value_t::boolean && y.type() == json::value_t::boolean) return x == y;
  if (x.is_string() && y.is_string()) return x == y;
  if (x.is_array() && y.is_array()) {
    if (x.size() != y.size()) return false;
    for (std::size_t i = 0; i < x.size(); i++) if (!deepEquals(x[i], y[i])) return false;
    return true;
  }
  if (x.is_object() && y.is_object()) {
    if (x.size() != y.size()) return false;
    for (auto it = x.begin(); it != x.end(); ++it) {
      if (!y.contains(it.key()) || !deepEquals(it.value(), y.at(it.key()))) return false;
    }
    return true;
  }
  return false;
}

int compareValues(const Value& a, const Value& b) {
  if (!a && !b) return 0;
  if (a && b && deepEquals(a, b) && a->type() == b->type()) return 0;
  if (nullish(a)) return (a && b && a->is_null() && b->is_null()) ? 0 : -1;
  if (nullish(b)) return 1;
  if (a->is_number() && b->is_number()) return sign(a->get<double>() - b->get<double>());
  if (a->is_string() && b->is_string()) return compareText(a->get<std::string>(), b->get<std::string>());
  if (a->is_boolean() && b->is_boolean()) {
    bool x = a->get<bool>(), y = b->get<bool>();
    return x == y ? 0 : (x ? 1 : -1);
  }
  return compareText(toText(a), toText(b));
}

bool matchesType(const Value& value, const std::string& expected) {
  if (expected == "string") return value && value->is_string();
  if (expected == "number" || expected == "int" || expected == "long" || expected == "double" ||
      expected == "decimal")
    return value && value->is_number();
  if (expected == "bool" || expected == "boolean") return value && value->is_boolean();
  if (expected == "object") return value && value->is_object();
  if (expected == "array") return value && value->is_array();
  if (expected == "null") return value && value->is_null();
  if (expected == "undefined") return !value;
  return false;
}

bool matchesFilterImpl(const json& doc, const json& filter);

bool matchOperator(const Value& value, const json& op) {
  for (auto it = op.begin(); it != op.end(); ++it) {
    const std::string& key = it.key();
    const json& operand = it.value();
    if (key == "$eq") {
      if (!deepEquals(value, operand)) return false;
    } else if (key == "$ne") {
      if (deepEquals(value, operand)) return false;
    } else if (key == "$gt") {
      if (compareValues(value, operand) <= 0) return false;
    } else if (key == "$gte") {
      if (compareValues(value, operand) < 0) return false;
    } else if (key == "$lt") {
      if (compareValues(value, operand) >= 0) return false;
    } else if (key == "$lte") {
      if (compareValues(value, operand) > 0) return false;
    } else if (key == "$in" || key == "$nin") {
      auto contains = [&](const Value& v) {
        for (const auto& item : operand) if (deepEquals(v, item)) return true;
        return false;
      };
      bool any = false;
      if (value && value->is_array()) {
        for (const auto& v : *value) if (contains(v)) { any = true; break; }
      } else {
        any = contains(value);
      }
      if (key == "$in" ? !any : any) return false;
    } else if (key == "$regex") {
      if (!value || !value->is_string()) return false;
      std::regex re(operand.get<std::string>(), std::regex::ECMAScript);
      if (!std::regex_search(value->get<std::string>(), re)) return false;
    } else if (key == "$exists") {
      bool want = operand.is_boolean() ? operand.get<bool>() : !operand.is_null();
      if (want && !value) return false;
      if (!want && value) return false;
    } else if (key == "$not") {
      if (matchOperator(value, operand)) return false;
    } else if (key == "$elemMatch") {
      if (!value || !value->is_array()) return false;
      bool any = false;
      for (const auto& elem : *value) if (matchesFilterImpl(elem, operand)) { any = true; break; }
      if (!any) return false;
    } else if (key == "$size") {
      if (!value || !value->is_array() || !operand.is_number() ||
          static_cast<double>(value->size()) != operand.get<double>())
        return false;
    } else if (key == "$all") {
      if (!value || !value->is_array()) return false;
      for (const auto& item : operand) {
        bool found = false;
        for (const auto& v : *value) if (deepEquals(v, item)) { found = true; break; }
        if (!found) return false;
      }
    } else if (key == "$type") {
      if (!matchesType(value, operand.get<std::string>())) return false;
    }
    // Unknown operators are ignored, as in the reference.
  }
  return true;
}

bool matchesFilterImpl(const json& doc, const json& filter) {
  if (!filter.is_object()) return true;
  for (auto it = filter.begin(); it != filter.end(); ++it) {
    const std::string& key = it.key();
    const json& condition = it.value();
    if (key == "$and") {
      for (const auto& clause : condition) if (!matchesFilterImpl(doc, clause)) return false;
      continue;
    }
    if (key == "$or") {
      bool any = false;
      for (const auto& clause : condition) if (matchesFilterImpl(doc, clause)) { any = true; break; }
      if (!any) return false;
      continue;
    }
    if (key == "$nor") {
      for (const auto& clause : condition) if (matchesFilterImpl(doc, clause)) return false;
      continue;
    }
    if (key == "$not") {
      if (matchesFilterImpl(doc, condition)) return false;
      continue;
    }
    if (key == "$text") continue;  // not supported; matches everything, like an unknown logical operator
    if (key == "$and" || key == "$or" || key == "$nor") continue;

    Value value = getNested(doc, key);
    if (condition.is_object()) {
      bool hasOps = false;
      for (auto k = condition.begin(); k != condition.end(); ++k) if (!k.key().empty() && k.key()[0] == '$') hasOps = true;
      if (hasOps) {
        if (!matchOperator(value, condition)) return false;
        continue;
      }
    }
    if (value && value->is_array()) {
      bool any = false;
      for (const auto& v : *value) if (deepEquals(v, condition)) { any = true; break; }
      if (!any && !deepEquals(value, condition)) return false;
    } else if (!deepEquals(value, condition)) {
      return false;
    }
  }
  return true;
}

}  // namespace

bool matchesFilter(const json& document, const json& filter) { return matchesFilterImpl(document, filter); }

}  // namespace brightchain::v2
