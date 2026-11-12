#pragma once
// Replica merge (spec section 6): deterministic, so merging in either direction or any
// order gives the same root.

#include <optional>
#include <stdexcept>

#include "brightchain/v2/codec.hpp"
#include "brightchain/v2/disk_store.hpp"

namespace brightchain::v2 {

class MergeError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

struct MergeResult {
  Digest previous;     // the local root before the merge
  Digest root;         // the local root after; equal to `previous` when nothing changed
  std::size_t adopted = 0;  // records taken from the other replica
};

/// Merge `remoteHead` of the remote store into the local store while holding its write lock.
/// Returns nullopt when nothing changes; nothing is published unless every needed block was read
/// and verified. The caller publishes the returned root.
std::optional<std::pair<Digest, std::size_t>> mergeReplica(const DiskStore& local, DiskStore::Writer& writer,
                                                           const DiskStore& remote, const Digest& remoteHead);

/// Copy a whole database state into a store, children before parents.
void copyDatabase(const DiskStore& local, DiskStore::Writer& writer, const DiskStore& remote, const Digest& head);

}  // namespace brightchain::v2
