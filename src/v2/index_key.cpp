#include "brightchain/v2/index_key.hpp"

#include <cmath>
#include <cstring>
#include <set>

namespace brightchain::v2 {

namespace {

constexpr std::uint8_t kTagAbsent = 0x08, kTagNull = 0x10, kTagFalse = 0x20, kTagTrue = 0x21, kTagNumber = 0x30,
                       kTagString = 0x40;
constexpr std::size_t kMaxCombinations = 1024;

Bytes escapeString(const std::string& value) {
  if (!isValidUtf8(value)) throw IndexError("An indexed string must be well-formed Unicode");
  Bytes out;
  for (unsigned char c : value) {
    out.push_back(c);
    if (c == 0x00) out.push_back(0xff);
  }
  return out;
}

void append(Bytes& to, const Bytes& from) { to.insert(to.end(), from.begin(), from.end()); }

std::size_t componentLength(const Bytes& key, std::size_t offset) {
  if (offset >= key.size()) throw IndexError("Truncated index key");
  switch (key[offset]) {
    case kTagAbsent: case kTagNull: case kTagFalse: case kTagTrue: return 1;
    case kTagNumber: return 9;
    case kTagString: {
      for (std::size_t i = offset + 1; i < key.size(); i++) {
        if (key[i] == 0x00) {
          if (i + 1 < key.size() && key[i + 1] == 0xff) { i++; continue; }
          return i + 1 - offset;
        }
      }
      throw IndexError("Unterminated string in an index key");
    }
    default: throw IndexError("Unknown value tag in an index key");
  }
}

std::vector<IndexValue> fieldValues(const nlohmann::json& document, const std::string& path) {
  const nlohmann::json* current = &document;
  std::size_t start = 0;
  while (true) {
    auto dot = path.find('.', start);
    auto part = path.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    if (!current->is_object() || !current->contains(part)) return {std::nullopt};
    current = &current->at(part);
    if (dot == std::string::npos) break;
    start = dot + 1;
  }
  if (current->is_array()) {
    if (current->empty()) return {std::nullopt};
    std::vector<IndexValue> out;
    for (const auto& element : *current) {
      if (element.is_object() || element.is_array())
        throw IndexError("Field " + path + " holds an array element that cannot be indexed");
      out.push_back(element);
    }
    return out;
  }
  if (current->is_object()) throw IndexError("Field " + path + " holds an object that cannot be indexed");
  return {*current};
}

}  // namespace

IndexQuery IndexQuery::fromJson(const nlohmann::json& j) {
  IndexQuery q;
  if (j.contains("equals")) for (const auto& v : j.at("equals")) q.equals.push_back(v);
  for (auto [name, slot] : {std::pair<const char*, std::optional<nlohmann::json> IndexQuery::*>{"gt", &IndexQuery::gt},
                            {"gte", &IndexQuery::gte}, {"lt", &IndexQuery::lt}, {"lte", &IndexQuery::lte}}) {
    if (j.contains(name)) q.*slot = j.at(name);
  }
  if (j.contains("prefix")) q.prefix = j.at("prefix").get<std::string>();
  if (j.contains("descending")) q.descending = j.at("descending").get<bool>();
  if (j.contains("limit")) q.limit = j.at("limit").get<std::size_t>();
  return q;
}

Bytes encodeIndexValue(const IndexValue& value) {
  if (!value) return {kTagAbsent};
  const auto& v = *value;
  if (v.is_null()) return {kTagNull};
  if (v.is_boolean()) return {v.get<bool>() ? kTagTrue : kTagFalse};
  if (v.is_number()) {
    double d = v.get<double>();
    if (!std::isfinite(d)) throw IndexError("An indexed number must be finite");
    if (d == 0) d = 0.0;  // -0 sorts with 0
    std::uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    bits = (bits >> 63) ? ~bits : (bits | (1ULL << 63));
    Bytes out{kTagNumber};
    for (int i = 0; i < 8; i++) out.push_back(std::uint8_t(bits >> (56 - 8 * i)));
    return out;
  }
  if (v.is_string()) {
    Bytes out{kTagString};
    append(out, escapeString(v.get<std::string>()));
    out.push_back(0x00);
    return out;
  }
  throw IndexError("Only scalar values can be indexed");
}

std::optional<Bytes> prefixSuccessor(const Bytes& prefix) {
  Bytes out = prefix;
  while (!out.empty() && out.back() == 0xff) out.pop_back();
  if (out.empty()) return std::nullopt;
  out.back()++;
  return out;
}

std::string decodeIndexId(const Bytes& key, std::size_t fieldCount) {
  std::size_t offset = 0;
  for (std::size_t i = 0; i < fieldCount; i++) offset += componentLength(key, offset);
  if (offset >= key.size() || key[offset] != kTagString) throw IndexError("An index key must end with a document _id");
  auto end = offset + componentLength(key, offset);
  if (end != key.size()) throw IndexError("Trailing bytes in an index key");
  std::string out;
  for (std::size_t i = offset + 1; i + 1 < end; i++) {
    out.push_back(char(key[i]));
    if (key[i] == 0x00) i++;  // skip the 0xff escape
  }
  return out;
}

std::vector<Bytes> extractIndexKeys(const nlohmann::json& document, const std::vector<std::string>& fields,
                                    const std::string& id) {
  auto idBytes = encodeIndexValue(nlohmann::json(id));
  std::vector<Bytes> combinations{Bytes{}};
  for (const auto& field : fields) {
    std::set<Bytes> unique;
    std::vector<Bytes> encoded;
    for (const auto& value : fieldValues(document, field)) {
      auto e = encodeIndexValue(value);
      if (unique.insert(e).second) encoded.push_back(std::move(e));
    }
    std::vector<Bytes> next;
    for (const auto& prefix : combinations) {
      for (const auto& value : encoded) {
        Bytes joined = prefix;
        append(joined, value);
        next.push_back(std::move(joined));
      }
    }
    if (next.size() > kMaxCombinations) throw IndexError("A document produces too many index entries");
    combinations = std::move(next);
  }
  for (auto& key : combinations) {
    append(key, idBytes);
    if (key.size() > kMaxIndexKeyBytes) throw IndexError("An index key exceeds 2048 bytes");
  }
  return combinations;
}

IndexKeyRange indexRange(std::size_t fieldCount, const IndexQuery& query) {
  const auto& equals = query.equals;
  if (equals.size() > fieldCount) throw IndexError("More equality values than index fields");
  int bounds = int(query.gt.has_value()) + int(query.gte.has_value()) + int(query.lt.has_value()) +
               int(query.lte.has_value());
  bool hasBounds = bounds > 0 || query.prefix.has_value();
  if (hasBounds && equals.size() >= fieldCount) throw IndexError("No index field is left for a range bound");
  if (query.prefix && bounds > 0) throw IndexError("prefix cannot be combined with other bounds");
  if (query.gt && query.gte) throw IndexError("Use either gt or gte");
  if (query.lt && query.lte) throw IndexError("Use either lt or lte");

  Bytes base;
  for (const auto& v : equals) append(base, encodeIndexValue(v));
  auto baseEnd = [&]() -> std::optional<Bytes> {
    if (!equals.empty() && equals.back().is_string()) {
      Bytes b = base;
      b.push_back(0xff);
      return b;
    }
    return prefixSuccessor(base);
  };

  if (query.prefix) {
    Bytes lo = base;
    lo.push_back(kTagString);
    append(lo, escapeString(*query.prefix));
    return {lo, prefixSuccessor(lo)};
  }
  if (!hasBounds) return {base, baseEnd()};

  auto withValue = [&](const nlohmann::json& v) {
    Bytes b = base;
    append(b, encodeIndexValue(v));
    return b;
  };
  auto afterValue = [&](const nlohmann::json& v) -> std::optional<Bytes> {
    Bytes b = withValue(v);
    if (v.is_string()) { b.push_back(0xff); return b; }
    return prefixSuccessor(b);
  };
  auto tagOf = [&](const nlohmann::json& v) {
    Bytes b = base;
    b.push_back(encodeIndexValue(v).front());
    return b;
  };

  std::optional<Bytes> lo;
  std::optional<std::optional<Bytes>> hi;
  if (query.gte) lo = withValue(*query.gte);
  if (query.gt) lo = *afterValue(*query.gt);
  if (query.lte) hi = afterValue(*query.lte);
  if (query.lt) hi = std::optional<Bytes>(withValue(*query.lt));
  const auto& lower = query.gte ? query.gte : query.gt;
  const auto& upper = query.lte ? query.lte : query.lt;
  if (!lo) lo = tagOf(*upper);
  if (!hi) hi = prefixSuccessor(tagOf(*lower));
  return {*lo, *hi};
}

}  // namespace brightchain::v2
