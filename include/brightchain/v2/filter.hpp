#pragma once
// MongoDB-style filter matching with the same semantics as the TypeScript query engine
// (brightchain-db queryEngine.ts): $and $or $nor $not, $eq $ne $gt $gte $lt $lte $in $nin
// $regex $exists $elemMatch $size $all $type, dotted paths and array traversal.
//
// One deliberate difference: TypeScript orders strings with the host's locale collation
// (String.prototype.localeCompare); here strings compare bytewise on their UTF-8. The two agree
// for lowercase ASCII and digits, which the shared tests use. Mixed-type ordering converts both
// sides to text first, as the reference does.

#include <nlohmann/json.hpp>

namespace brightchain::v2 {

bool matchesFilter(const nlohmann::json& document, const nlohmann::json& filter);

}  // namespace brightchain::v2
