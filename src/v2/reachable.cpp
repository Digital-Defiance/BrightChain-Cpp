#include "brightchain/v2/reachable.hpp"

#include <functional>

namespace brightchain::v2 {

namespace {

struct Walker {
  const BlockSource& source;
  std::set<Digest> seen;

  Metadata meta(const Digest& d) { return decodeMetadata(source.readBlock(d), d); }

  void blob(const Digest& d) {
    if (!seen.insert(d).second) return;
    auto m = meta(d);
    const auto* manifest = std::get_if<Blob>(&m);
    if (!manifest) throw CodecError("Not an attachment manifest");
    for (const auto& entry : manifest->entries) {
      if (manifest->depth > 0) blob(entry); else seen.insert(entry);
    }
  }

  void document(const Digest& d) {
    if (!seen.insert(d).second) return;
    auto doc = decodeDocument(source.readBlock(d), std::nullopt, d);
    if (doc.contains("_files") && doc.at("_files").is_object())
      for (const auto& entry : doc.at("_files")) blob(entry.at("digest").get<std::string>());
  }

  void indexTree(const Digest& d) {
    if (!seen.insert(d).second) return;
    auto page = meta(d);
    if (const auto* branch = std::get_if<IndexBranch>(&page)) for (const auto& c : branch->children) indexTree(c.digest);
  }

  void tree(LeafKind kind, const Digest& d) {
    if (!seen.insert(d).second) return;
    auto page = meta(d);
    if (const auto* branch = std::get_if<Branch>(&page)) {
      for (const auto& c : branch->children) tree(kind, c.digest);
    } else if (const auto* leaf = std::get_if<Leaf>(&page); leaf && leaf->kind == kind) {
      for (const auto& r : leaf->records) {
        if (!r.target) continue;
        if (kind == LeafKind::Catalog) collection(*r.target); else document(*r.target);
      }
    }
  }

  void collection(const Digest& d) {
    if (!seen.insert(d).second) return;
    auto m = meta(d);
    const auto* root = std::get_if<CollectionRoot>(&m);
    if (!root) throw CodecError("Not a collection root");
    if (root->primary) tree(LeafKind::Primary, *root->primary);
    if (root->indexes && seen.insert(*root->indexes).second) {
      auto set = meta(*root->indexes);
      if (const auto* definitions = std::get_if<IndexSet>(&set))
        for (const auto& def : definitions->definitions) if (def.root) indexTree(*def.root);
    }
  }
};

}  // namespace

std::set<Digest> collectReachable(const BlockSource& source, const std::vector<Digest>& roots) {
  Walker walker{source, {}};
  for (const auto& r : roots) {
    if (!walker.seen.insert(r).second) continue;
    auto m = walker.meta(r);
    const auto* root = std::get_if<DatabaseRoot>(&m);
    if (!root) throw CodecError("Not a database root");
    if (root->catalog) walker.tree(LeafKind::Catalog, *root->catalog);
  }
  return std::move(walker.seen);
}

}  // namespace brightchain::v2
