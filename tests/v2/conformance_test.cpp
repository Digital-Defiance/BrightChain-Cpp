// Conformance tests for BrightDB storage format v2, driven by the golden
// fixtures published by the TypeScript reference implementation.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>

#include "brightchain/v2/database.hpp"

using namespace brightchain::v2;
using nlohmann::json;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = BRIGHTDB_V2_FIXTURES;

json loadJson(const fs::path& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("cannot read " + path.string());
  return json::parse(in);
}

Version versionFrom(const json& j) {
  return Version{std::stoull(j.at("counter").get<std::string>()), j.at("writerId").get<std::string>()};
}

std::optional<Digest> refFrom(const json& j) {
  if (j.is_null()) return std::nullopt;
  return j.get<std::string>();
}

Record recordFrom(const json& j) {
  return Record{j.at("key").get<std::string>(), versionFrom(j.at("version")), refFrom(j.at("target"))};
}

Metadata metadataFrom(const json& j) {
  auto kind = j.at("kind").get<std::string>();
  if (kind == "database")
    return DatabaseRoot{j.at("databaseId"), std::stoull(j.at("clock").get<std::string>()), refFrom(j.at("catalog"))};
  if (kind == "collection") return CollectionRoot{versionFrom(j.at("generation")), refFrom(j.at("primary"))};
  if (kind == "branch") {
    Branch b;
    for (const auto& c : j.at("children"))
      b.children.push_back({c.at("nibble"), std::stoull(c.at("recordBytes").get<std::string>()), c.at("digest")});
    return b;
  }
  Leaf leaf;
  leaf.kind = kind == "primary" ? LeafKind::Primary : LeafKind::Catalog;
  for (const auto& r : j.at("records")) leaf.records.push_back(recordFrom(r));
  return leaf;
}

struct MemoryBlocks : BlockStore {
  std::map<Digest, Bytes> blocks;
  Bytes get(const Digest& d) override {
    auto it = blocks.find(d);
    if (it == blocks.end()) throw MissingBlockError(d);
    return it->second;
  }
  void put(const Digest& d, const Bytes& b) override {
    EXPECT_EQ(digestPayload(b), d);
    blocks[d] = b;
  }
};

struct TempDir {
  fs::path path;
  TempDir() {
    path = fs::temp_directory_path() / ("brightdb-v2-cpp-" + std::to_string(std::random_device{}()));
    fs::create_directories(path);
  }
  ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};

json sortedById(json docs) {
  std::vector<json> v(docs.begin(), docs.end());
  std::sort(v.begin(), v.end(), [](const json& a, const json& b) {
    return compareKeys(a.at("_id"), b.at("_id")) < 0;
  });
  return v;
}

}  // namespace

TEST(V2Vectors, EveryBlockMatchesTheReferenceBytesAndDigests) {
  auto vectors = loadJson(kFixtures / "vectors.json");
  int checked = 0;
  for (const auto& block : vectors.at("blocks")) {
    auto name = block.at("name").get<std::string>();
    auto payload = fromHex(block.at("payload"));
    EXPECT_EQ(digestPayload(payload), block.at("digest").get<std::string>()) << name;

    bool isMetadata = block.contains("decoded") || name == "primary" || name == "collection" ||
                      name == "catalog" || name == "database" || name == "empty" || name == "branch";
    if (isMetadata) {
      auto input = metadataFrom(block.at("input"));
      EXPECT_EQ(encodeMetadata(input), payload) << "encode " << name;
      EXPECT_EQ(encodeMetadata(decodeMetadata(payload, block.at("digest").get<std::string>())), payload) << name;
    } else {
      const auto& in = block.at("input");
      json doc = in.contains("json") ? json::parse(in.at("json").get<std::string>()) : in;
      EXPECT_EQ(encodeDocument(doc), payload) << "encode " << name;
      EXPECT_EQ(decodeDocument(payload, doc.at("_id").get<std::string>()), doc) << name;
    }
    checked++;
  }
  EXPECT_GE(checked, 11);
}

TEST(V2Vectors, DocumentsThatMustBeRejectedAreRejected) {
  auto vectors = loadJson(kFixtures / "vectors.json");
  for (const auto& entry : vectors.at("rejectedDocuments")) {
    auto text = entry.at("json").get<std::string>();
    bool rejected = false;
    try {
      auto parsed = json::parse(text);
      encodeDocument(parsed);
    } catch (const std::exception&) {
      rejected = true;
    }
    EXPECT_TRUE(rejected) << entry.at("name") << ": " << text;
  }
}

TEST(V2Vectors, TreesAreCanonicalRegardlessOfInsertionOrder) {
  auto vectors = loadJson(kFixtures / "vectors.json");
  for (const auto& tree : vectors.at("trees")) {
    auto kind = tree.at("kind").get<std::string>() == "primary" ? LeafKind::Primary : LeafKind::Catalog;
    std::vector<Record> records;
    for (const auto& r : tree.at("records")) records.push_back(recordFrom(r));

    for (unsigned seed : {1u, 2u, 3u}) {
      auto shuffled = records;
      std::mt19937 rng(seed);
      std::shuffle(shuffled.begin(), shuffled.end(), rng);

      MemoryBlocks bulk;
      Tree bulkTree(bulk, kind);
      auto root = bulkTree.setMany(std::nullopt, shuffled);
      ASSERT_TRUE(root.has_value());
      EXPECT_EQ(*root, tree.at("root").get<std::string>()) << tree.at("name") << " bulk seed " << seed;

      MemoryBlocks incremental;
      Tree incTree(incremental, kind);
      std::optional<Digest> current;
      for (const auto& r : shuffled) current = incTree.setMany(current, {r});
      EXPECT_EQ(current, root) << tree.at("name") << " incremental seed " << seed;
    }

    MemoryBlocks blocks;
    Tree t(blocks, kind);
    auto root = t.setMany(std::nullopt, records);
    for (const auto& page : tree.at("pages")) {
      auto digest = page.at("digest").get<std::string>();
      ASSERT_TRUE(blocks.blocks.count(digest)) << "missing page " << digest;
      EXPECT_EQ(blocks.blocks[digest], fromHex(page.at("payload")));
    }
    // Rewriting one record must keep every unrelated page digest.
    auto before = blocks.blocks;
    auto changed = records.front();
    changed.version.counter += 100;
    auto next = t.setMany(root, {changed});
    EXPECT_NE(next, root);
    std::size_t added = 0;
    for (const auto& [d, _] : blocks.blocks) if (!before.count(d)) added++;
    EXPECT_LE(added, 4u);
  }
}

TEST(V2Store, ReadsTheStoreWrittenByTypeScript) {
  auto manifest = loadJson(kFixtures / "stores" / "basic.json");
  TempDir tmp;
  fs::copy(kFixtures / "stores" / "basic", tmp.path / "db", fs::copy_options::recursive);
  auto db = Database::open((tmp.path / "db").string());

  auto names = db.listCollections();
  std::sort(names.begin(), names.end());
  std::vector<std::string> expected;
  for (auto it = manifest.at("collections").begin(); it != manifest.at("collections").end(); ++it)
    expected.push_back(it.key());
  std::sort(expected.begin(), expected.end());
  EXPECT_EQ(names, expected);

  for (auto it = manifest.at("collections").begin(); it != manifest.at("collections").end(); ++it) {
    for (const auto& doc : it.value()) {
      auto found = db.findById(it.key(), doc.at("_id"));
      ASSERT_TRUE(found.has_value()) << it.key() << "/" << doc.at("_id");
      EXPECT_EQ(*found, doc);
    }
    EXPECT_EQ(sortedById(json(db.findAll(it.key()))), sortedById(it.value())) << it.key();
  }
  for (auto it = manifest.at("absent").begin(); it != manifest.at("absent").end(); ++it)
    for (const auto& id : it.value()) EXPECT_FALSE(db.findById(it.key(), id).has_value()) << id;
}

TEST(V2Store, ReproducesEveryTypeScriptHeadByteForByte) {
  auto manifest = loadJson(kFixtures / "stores" / "basic.json");
  TempDir tmp;
  auto path = (tmp.path / "db").string();
  DatabaseRoot empty{manifest.at("databaseId"), 0, std::nullopt};
  auto store = DiskStore::create(path, encodeMetadata(empty), manifest.at("writerId"));
  auto db = Database::open(path);
  std::vector<Digest> heads{store.readHead()};
  auto mark = [&] { heads.push_back(DiskStore::open(path).readHead()); };

  using T = WriteOp::Type;
  db.writeBatch({{T::Insert, "users", "", {{"_id", "alice"}, {"age", 31}}},
                 {T::Insert, "users", "", {{"_id", "bob"}, {"age", 27}}},
                 {T::Insert, "notes", "", {{"_id", "n1"}, {"text", "first"}}}});
  mark();
  db.replaceById("users", "bob", {{"_id", "bob"}, {"age", 28}});
  mark();
  db.deleteById("users", "alice");
  mark();
  std::vector<json> items;
  for (int i = 0; i < 200; i++) {
    char id[32];
    std::snprintf(id, sizeof id, "item-%04d", i);
    items.push_back({{"_id", id}, {"index", i}, {"label", "Item " + std::to_string(i) + " \xC3\xA9\xF0\x9F\x98\x80"}});
  }
  db.insertMany("items", items);
  mark();
  db.writeBatch({{T::DropCollection, "notes", "", {}}});
  mark();
  db.writeBatch({{T::Insert, "notes", "", {{"_id", "n2"}, {"text", "second generation"}}},
                 {T::Delete, "items", "item-0007", {}},
                 {T::Insert, "unicode", "", {{"_id", "\xF0\x9F\x98\x80"}, {"v", json::array({1, 2.5, "x"})}}}});
  mark();

  auto expectedHeads = manifest.at("heads").get<std::vector<std::string>>();
  ASSERT_EQ(heads.size(), expectedHeads.size());
  for (std::size_t i = 0; i < heads.size(); i++) EXPECT_EQ(heads[i], expectedHeads[i]) << "commit " << i;
}

TEST(V2Store, RejectsMalformedDirectoriesAndMissingBlocks) {
  TempDir tmp;
  EXPECT_THROW(DiskStore::open((tmp.path / "absent").string()), StorageError);
  fs::create_directories(tmp.path / "legacy");
  std::ofstream(tmp.path / "legacy" / "head.json") << "{}";
  EXPECT_THROW(Database::open((tmp.path / "legacy").string()), StorageError);

  auto path = (tmp.path / "db").string();
  auto db = Database::open(path);
  db.insertOne("c", {{"_id", "a"}, {"n", 1}});
  EXPECT_THROW(db.insertOne("c", {{"_id", "a"}}), DuplicateKeyError);
  auto head = DiskStore::open(path).readHead();
  EXPECT_EQ(DiskStore::open(path).readHead(), head);  // failed insert published nothing
  for (auto& e : fs::recursive_directory_iterator(tmp.path / "db" / "blocks"))
    if (e.is_regular_file() && e.path().filename().string() != head) fs::remove(e.path());
  EXPECT_THROW(db.findById("c", "a"), StorageError);
}
