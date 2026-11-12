#pragma once
// Order-preserving, prefix-free key encoding for secondary indexes (spec section 7).

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "brightchain/v2/codec.hpp"

namespace brightchain::v2 {

class IndexError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

/// An indexed value. nullopt is an absent field; otherwise a JSON scalar.
using IndexValue = std::optional<nlohmann::json>;

struct IndexQuery {
  /// Exact values for the leading fields.
  std::vector<nlohmann::json> equals;
  /// Bounds on the field after `equals`. A JSON null bound is a real value.
  std::optional<nlohmann::json> gt, gte, lt, lte;
  /// Strings starting with this value, on the field after `equals`.
  std::optional<std::string> prefix;
  bool descending = false;
  std::optional<std::size_t> limit;

  /// Parse {equals, gt, gte, lt, lte, prefix, descending, limit}.
  static IndexQuery fromJson(const nlohmann::json& j);
};

struct IndexKeyRange {
  Bytes lo;                 // inclusive; empty means unbounded below
  std::optional<Bytes> hi;  // exclusive; nullopt means unbounded above
};

Bytes encodeIndexValue(const IndexValue& value);
/// Smallest byte string greater than every string with this prefix, or nullopt.
std::optional<Bytes> prefixSuccessor(const Bytes& prefix);
/// The document _id at the end of an entry key of an index with `fieldCount` fields.
std::string decodeIndexId(const Bytes& key, std::size_t fieldCount);
/// Entry keys for one document: one per distinct array element, "absent" for missing fields.
std::vector<Bytes> extractIndexKeys(const nlohmann::json& document, const std::vector<std::string>& fields,
                                    const std::string& id);
IndexKeyRange indexRange(std::size_t fieldCount, const IndexQuery& query);

}  // namespace brightchain::v2
