#pragma once
// Which blocks a set of roots keeps alive: database root, catalog and collection roots, primary
// trees, documents, index definitions and trees, and attachment manifests and chunks (through `_files`).

#include <set>
#include <vector>

#include "brightchain/v2/block_source.hpp"
#include "brightchain/v2/codec.hpp"

namespace brightchain::v2 {

std::set<Digest> collectReachable(const BlockSource& source, const std::vector<Digest>& roots);

}  // namespace brightchain::v2
