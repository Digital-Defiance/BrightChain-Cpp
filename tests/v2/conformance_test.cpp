// Conformance tests for BrightDB storage format v2, driven by the golden
// fixtures published by the TypeScript reference implementation.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <cstring>
#include <random>
#include <sstream>

#include "brightchain/v2/database.hpp"
#include "brightchain/v2/index_key.hpp"
#include "brightchain/v2/index_tree.hpp"

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
  if (kind == "collection")
    return CollectionRoot{versionFrom(j.at("generation")), refFrom(j.at("primary")), refFrom(j.at("indexes"))};
  if (kind == "indexes") {
    IndexSet set;
    for (const auto& d : j.at("definitions"))
      set.definitions.push_back({d.at("name"), d.at("fields").get<std::vector<std::string>>(), refFrom(d.at("root"))});
    return set;
  }
  if (kind == "index-leaf") {
    IndexLeaf leaf;
    for (const auto& e : j.at("entries")) leaf.entries.push_back(fromHex(e.get<std::string>()));
    return leaf;
  }
  if (kind == "index-branch") {
    IndexBranch b;
    b.skip = j.at("skip").get<std::vector<int>>();
    for (const auto& c : j.at("children"))
      b.children.push_back({c.at("nibble"), std::stoull(c.at("recordBytes").get<std::string>()), c.at("digest")});
    return b;
  }
  if (kind == "blob") {
    Blob blob;
    blob.size = std::stoull(j.at("size").get<std::string>());
    blob.depth = j.at("depth");
    blob.entries = j.at("entries").get<std::vector<std::string>>();
    return blob;
  }
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

    bool isMetadata = block.at("type").get<std::string>() == "metadata";
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
  EXPECT_GE(checked, 19);
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

TEST(V2Diff, RootsSnapshotsAndChanges) {
  TempDir tmp;
  auto db = Database::open((tmp.path / "db").string());
  auto empty = db.root();
  db.insertMany("users", {{{"_id", "a"}, {"n", 1}}, {{"_id", "b"}, {"n", 2}}, {{"_id", "c"}, {"n", 3}}});
  auto before = db.root();
  EXPECT_NE(empty, before);

  using T = WriteOp::Type;
  db.writeBatch({{T::Replace, "users", "a", {{"_id", "a"}, {"n", 10}}},
                 {T::Delete, "users", "b", {}},
                 {T::Insert, "users", "", {{"_id", "d"}, {"n", 4}}},
                 {T::Insert, "logs", "", {{"_id", "l1"}, {"m", "x"}}}});
  auto after = db.root();

  auto summarize = [](const std::vector<Change>& changes) {
    std::vector<std::string> out;
    for (const auto& c : changes) {
      const char* t = c.type == Change::Type::Added ? "added" : c.type == Change::Type::Modified ? "modified" : "deleted";
      out.push_back(c.collection + "/" + c.id + ":" + t);
    }
    std::sort(out.begin(), out.end());
    return out;
  };
  EXPECT_EQ(summarize(db.diff(before, after)),
            (std::vector<std::string>{"logs/l1:added", "users/a:modified", "users/b:deleted", "users/d:added"}));
  EXPECT_EQ(summarize(db.diff(after, before)),
            (std::vector<std::string>{"logs/l1:deleted", "users/a:modified", "users/b:added", "users/d:deleted"}));
  EXPECT_TRUE(db.diff(after, after).empty());

  // Past states stay readable and snapshots stay pinned.
  auto old = db.at(before);
  EXPECT_EQ(old.findById("users", "a")->at("n"), 1);
  EXPECT_TRUE(old.findById("users", "d") == std::nullopt);
  EXPECT_TRUE(db.at(empty).listCollections().empty());
  EXPECT_THROW(db.at(std::string(128, 'f')), StorageError);

  for (const auto& c : db.diff(before, after)) {
    if (c.id == "a") {
      EXPECT_EQ(c.before->at("n"), 1);
      EXPECT_EQ(c.after->at("n"), 10);
    }
  }
}

TEST(V2Diff, DropAndRecreateReportsDeletionsThenAdditions) {
  TempDir tmp;
  auto db = Database::open((tmp.path / "db").string());
  db.insertOne("c", {{"_id", "old"}, {"v", 1}});
  auto before = db.root();
  using T = WriteOp::Type;
  db.writeBatch({{T::DropCollection, "c", "", {}}, {T::Insert, "c", "", {{"_id", "new"}, {"v", 2}}}});
  std::vector<std::string> got;
  for (const auto& c : db.diff(before, db.root()))
    got.push_back(c.id + ":" + (c.type == Change::Type::Added ? "added" : "deleted"));
  std::sort(got.begin(), got.end());
  EXPECT_EQ(got, (std::vector<std::string>{"new:added", "old:deleted"}));
}

TEST(V2Diff, DiffOfLargeCollectionReadsOnlyChangedPaths) {
  TempDir tmp;
  auto path = (tmp.path / "db").string();
  auto db = Database::open(path);
  std::vector<json> docs;
  for (int i = 0; i < 800; i++) docs.push_back({{"_id", "u" + std::to_string(i)}, {"n", i}});
  db.insertMany("users", docs);
  auto before = db.root();
  db.replaceById("users", "u433", {{"_id", "u433"}, {"n", -1}});
  auto changes = db.diff(before, db.root());
  ASSERT_EQ(changes.size(), 1u);
  EXPECT_EQ(changes[0].id, "u433");
  EXPECT_EQ(changes[0].type, Change::Type::Modified);
}

TEST(V2Subscribe, DeliversLocalAndCrossHandleCommitsAndStops) {
  TempDir tmp;
  auto path = (tmp.path / "db").string();
  auto db = Database::open(path);
  auto other = Database::open(path);  // a second handle, as another process would have

  std::mutex m;
  std::vector<std::string> seen;
  std::vector<std::string> errors;
  auto sub = db.subscribe(
      [&](const ChangeEvent& event) {
        for (const auto& c : event.changes()) {
          std::lock_guard<std::mutex> lock(m);
          seen.push_back(c.id + (c.type == Change::Type::Added ? ":added" : c.type == Change::Type::Modified ? ":modified" : ":deleted"));
        }
        if (event.changes().size() == 1 && event.changes()[0].id == "boom") throw std::runtime_error("listener failed");
      },
      std::chrono::milliseconds(20), [&](const std::exception& e) { std::lock_guard<std::mutex> lock(m); errors.push_back(e.what()); });

  auto waitFor = [&](auto pred) {
    for (int i = 0; i < 500; i++) {
      { std::lock_guard<std::mutex> lock(m); if (pred()) return true; }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
  };

  db.insertOne("c", {{"_id", "a"}, {"v", 1}});
  ASSERT_TRUE(waitFor([&] { return seen.size() >= 1; }));
  other.replaceById("c", "a", {{"_id", "a"}, {"v", 2}});  // committed through the other handle
  ASSERT_TRUE(waitFor([&] { return seen.size() >= 2; }));
  EXPECT_EQ(seen, (std::vector<std::string>{"a:added", "a:modified"}));

  db.insertOne("c", {{"_id", "boom"}});
  ASSERT_TRUE(waitFor([&] { return !errors.empty(); }));
  EXPECT_EQ(errors[0], "listener failed");

  sub->stop();
  auto count = [&] { std::lock_guard<std::mutex> lock(m); return seen.size(); };
  auto before = count();
  db.deleteById("c", "a");
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  EXPECT_EQ(count(), before);
}

// ---- indexes -----------------------------------------------------------------

namespace {

IndexValue valueFrom(const std::string& type, const json& j) {
  if (type == "absent") return std::nullopt;
  return j;
}

Bytes bytesOf(const std::string& hex) { return fromHex(hex); }

std::vector<Bytes> keysFrom(const json& hexes) {
  std::vector<Bytes> out;
  for (const auto& h : hexes) out.push_back(fromHex(h.get<std::string>()));
  return out;
}

}  // namespace

TEST(V2Index, ValueEncodingsMatchTheReference) {
  auto vectors = loadJson(kFixtures / "vectors.json");
  for (const auto& v : vectors.at("indexValues"))
    EXPECT_EQ(toHex(encodeIndexValue(valueFrom(v.at("type"), v.at("json")))), v.at("hex").get<std::string>())
        << v.dump();
}

TEST(V2Index, EntryKeysAndQueryRangesMatchTheReference) {
  auto vectors = loadJson(kFixtures / "vectors.json");
  for (const auto& c : vectors.at("indexKeys")) {
    auto keys = extractIndexKeys(c.at("document"), c.at("fields").get<std::vector<std::string>>(), c.at("id"));
    std::vector<std::string> hexes;
    for (const auto& k : keys) hexes.push_back(toHex(k));
    std::sort(hexes.begin(), hexes.end());
    EXPECT_EQ(hexes, c.at("keys").get<std::vector<std::string>>()) << c.dump();
    for (const auto& k : keys) EXPECT_EQ(decodeIndexId(k, c.at("fields").size()), c.at("id").get<std::string>());
  }
  EXPECT_THROW(extractIndexKeys({{"a", {{"b", 1}}}}, {"a"}, "d"), IndexError);
  EXPECT_THROW(extractIndexKeys({{"a", json::array({json::array({1})})}}, {"a"}, "d"), IndexError);

  for (const auto& c : vectors.at("indexRanges")) {
    auto range = indexRange(c.at("fieldCount"), IndexQuery::fromJson(c.at("query")));
    EXPECT_EQ(toHex(range.lo), c.at("lo").get<std::string>()) << c.dump();
    if (c.at("hi").is_null()) EXPECT_FALSE(range.hi.has_value()) << c.dump();
    else {
      ASSERT_TRUE(range.hi.has_value()) << c.dump();
      EXPECT_EQ(toHex(*range.hi), c.at("hi").get<std::string>()) << c.dump();
    }
  }
}

TEST(V2Index, TreesMatchTheReferenceAndAreCanonical) {
  auto vectors = loadJson(kFixtures / "vectors.json");
  for (const auto& tree : vectors.at("indexTrees")) {
    auto keys = keysFrom(tree.at("keys"));
    for (unsigned seed : {1u, 2u}) {
      auto shuffled = keys;
      std::mt19937 rng(seed);
      std::shuffle(shuffled.begin(), shuffled.end(), rng);
      MemoryBlocks blocks;
      IndexTree t(blocks);
      auto root = t.apply(std::nullopt, shuffled, {});
      ASSERT_TRUE(root.has_value());
      EXPECT_EQ(*root, tree.at("root").get<std::string>()) << tree.at("name") << " seed " << seed;
      if (seed == 1) {
        for (const auto& page : tree.at("pages")) {
          auto digest = page.at("digest").get<std::string>();
          ASSERT_TRUE(blocks.blocks.count(digest)) << "missing page " << digest;
          EXPECT_EQ(blocks.blocks[digest], fromHex(page.at("payload")));
        }
      }
    }
    // Two-step build gives the same root.
    MemoryBlocks staged;
    IndexTree t(staged);
    auto half = keys.size() / 2;
    auto rolling = t.apply(std::nullopt, std::vector<Bytes>(keys.begin(), keys.begin() + std::ptrdiff_t(half)), {});
    rolling = t.apply(rolling, std::vector<Bytes>(keys.begin() + std::ptrdiff_t(half), keys.end()), {});
    EXPECT_EQ(rolling, std::optional<Digest>(tree.at("root").get<std::string>()));
  }
}

TEST(V2Index, RandomBatchesKeepTheTreeCanonical) {
  std::mt19937 rng(7);
  auto uniform = [&](int n) { return int(rng() % unsigned(n)); };
  auto makeKey = [&]() {
    int mode = uniform(3);
    json value = mode == 0 ? json("user-000000" + std::to_string(uniform(400)))
                 : mode == 1 ? json(1700000000000LL + uniform(3000))
                             : (uniform(50) == 0 ? json("zzzz") : json("k" + std::to_string(uniform(50))));
    auto k = encodeIndexValue(value);
    auto id = encodeIndexValue(json("id" + std::to_string(uniform(600))));
    k.insert(k.end(), id.begin(), id.end());
    return k;
  };
  MemoryBlocks blocks;
  IndexTree tree(blocks);
  std::set<Bytes> model;
  std::optional<Digest> root;
  for (int round = 0; round < 60; round++) {
    std::vector<Bytes> adds, removes;
    for (int i = 0, n = uniform(120); i < n; i++) adds.push_back(makeKey());
    for (const auto& k : model) if (uniform(100) < (round % 7 == 6 ? 70 : 8)) removes.push_back(k);
    for (int i = 0; i < 5; i++) removes.push_back(makeKey());
    root = tree.apply(root, adds, removes);
    std::set<Bytes> addSet(adds.begin(), adds.end());
    for (const auto& r : removes) if (!addSet.count(r)) model.erase(r);
    for (const auto& a : adds) model.insert(a);

    std::vector<Bytes> seen;
    tree.entries(root, [&](const Bytes& k) { seen.push_back(k); return true; });
    ASSERT_EQ(seen, std::vector<Bytes>(model.begin(), model.end())) << "round " << round;
    MemoryBlocks fresh;
    IndexTree freshTree(fresh);
    EXPECT_EQ(freshTree.apply(std::nullopt, std::vector<Bytes>(model.begin(), model.end()), {}), root) << "round " << round;
  }
}

// ---- the indexed store -------------------------------------------------------

namespace {

Bytes attachmentBytes(bool large) {
  if (!large) return Bytes{'h','e','l','l','o',' ','a','t','t','a','c','h','m','e','n','t'};
  Bytes b(1'048'577);
  for (std::size_t i = 0; i < b.size(); i++) b[i] = std::uint8_t(i % 251);
  return b;
}

json person(int index) {
  static const char* cities[] = {"Oslo", "Bergen", "Tromso", "Stavanger"};
  char id[16];
  std::snprintf(id, sizeof id, "p%04d", index);
  json tags = index % 3 == 0 ? json::array({"a", "b"}) : index % 3 == 1 ? json::array({"b"}) : json::array();
  return {{"_id", id}, {"name", "Person " + std::to_string(index)}, {"city", cities[index % 4]},
          {"age", 18 + ((index * 7) % 50)}, {"tags", tags}, {"profile", {{"score", (index * 13) % 100}}}};
}

std::vector<std::string> idsOf(const std::vector<json>& docs) {
  std::vector<std::string> out;
  for (const auto& d : docs) out.push_back(d.at("_id").get<std::string>());
  return out;
}

}  // namespace

TEST(V2Indexed, ReproducesEveryTypeScriptHeadAndAnswersTheRecordedQueries) {
  auto manifest = loadJson(kFixtures / "stores" / "indexed.json");
  TempDir tmp;
  auto path = (tmp.path / "db").string();
  DatabaseRoot empty{manifest.at("databaseId"), 0, std::nullopt};
  auto store = DiskStore::create(path, encodeMetadata(empty), manifest.at("writerId"));
  auto db = Database::open(path);
  std::vector<Digest> heads{store.readHead()};
  auto mark = [&] { heads.push_back(DiskStore::open(path).readHead()); };
  using T = WriteOp::Type;

  std::vector<WriteOp> first;
  for (int i = 0; i < 6; i++) first.push_back({T::Insert, "people", "", person(i)});
  first.push_back({T::CreateIndex, "people", "", {}, "by_city_age", {"city", "age"}});
  first.push_back({T::CreateIndex, "people", "", {}, "by_tag", {"tags"}});
  first.push_back({T::CreateIndex, "people", "", {}, "by_score", {"profile.score"}});
  db.writeBatch(first);
  mark();
  std::vector<json> more;
  for (int i = 6; i < 300; i++) more.push_back(person(i));
  db.insertMany("people", more);
  mark();
  auto changed = person(2);
  changed["city"] = "Oslo"; changed["age"] = 99; changed["tags"] = json::array({"z"});
  db.replaceById("people", "p0002", changed);
  mark();
  db.deleteById("people", "p0003");
  mark();
  auto small = db.putAttachment(attachmentBytes(false), "text/plain");
  auto large = db.putAttachment(attachmentBytes(true));
  EXPECT_EQ(small.digest, manifest.at("attachments").at("small").at("digest").get<std::string>());
  EXPECT_EQ(large.digest, manifest.at("attachments").at("large").at("digest").get<std::string>());
  db.insertOne("files", {{"_id", "with-files"},
                         {"_files", {{"note", {{"digest", small.digest}, {"size", small.size}, {"type", "text/plain"}}},
                                     {"big", {{"digest", large.digest}, {"size", large.size}}}}}});
  mark();
  db.dropIndex("people", "by_tag");
  mark();
  db.writeBatch({{T::DropCollection, "files", "", {}},
                 {T::Insert, "files", "", {{"_id", "after-drop"}, {"v", 1}}},
                 {T::CreateIndex, "files", "", {}, "by_v", {"v"}}});
  mark();

  auto expectedHeads = manifest.at("heads").get<std::vector<std::string>>();
  ASSERT_EQ(heads.size(), expectedHeads.size());
  for (std::size_t i = 0; i < heads.size(); i++) EXPECT_EQ(heads[i], expectedHeads[i]) << "commit " << i;

  for (const auto& q : manifest.at("queries")) {
    auto docs = db.findByIndex(q.at("collection"), q.at("index"), IndexQuery::fromJson(q.at("query")));
    EXPECT_EQ(idsOf(docs), q.at("ids").get<std::vector<std::string>>()) << q.dump();
  }
  EXPECT_EQ(toHex(db.getAttachment(large.digest)), toHex(attachmentBytes(true)));
}

TEST(V2Indexed, ReadsTheStoreWrittenByTypeScript) {
  auto manifest = loadJson(kFixtures / "stores" / "indexed.json");
  TempDir tmp;
  fs::copy(kFixtures / "stores" / "indexed", tmp.path / "db", fs::copy_options::recursive);
  auto db = Database::open((tmp.path / "db").string());
  for (const auto& q : manifest.at("queries")) {
    auto docs = db.findByIndex(q.at("collection"), q.at("index"), IndexQuery::fromJson(q.at("query")));
    EXPECT_EQ(idsOf(docs), q.at("ids").get<std::vector<std::string>>()) << q.dump();
  }
  // A write made here keeps the index current for later readers.
  db.insertOne("people", {{"_id", "zz"}, {"city", "Oslo"}, {"age", 31}, {"tags", json::array()}, {"profile", {{"score", 1}}}});
  auto docs = db.findByIndex("people", "by_city_age", IndexQuery::fromJson({{"equals", {"Oslo"}}, {"gte", 31}, {"lte", 31}}));
  auto ids = idsOf(docs);
  EXPECT_TRUE(std::find(ids.begin(), ids.end(), "zz") != ids.end());
  auto info = db.listIndexes("people");
  ASSERT_EQ(info.size(), 2u);
  EXPECT_EQ(info[0].name, "by_city_age");
}

TEST(V2Indexed, MaintainsIndexesAtomicallyAndRejectsUnindexableWrites) {
  TempDir tmp;
  auto db = Database::open((tmp.path / "db").string());
  db.createIndex("users", "by_age", {"age"});
  db.insertOne("users", {{"_id", "a"}, {"age", 1}});
  auto before = db.root();
  using T = WriteOp::Type;
  EXPECT_THROW(db.writeBatch({{T::Insert, "users", "", {{"_id", "b"}, {"age", 2}}},
                              {T::Insert, "users", "", {{"_id", "c"}, {"age", {{"nested", true}}}}}}),
               IndexError);
  EXPECT_EQ(db.root(), before);
  EXPECT_EQ(idsOf(db.findByIndex("users", "by_age")), std::vector<std::string>{"a"});
  db.writeBatch({{T::DropIndex, "users", "", {}, "by_age", {}}, {T::CreateIndex, "users", "", {}, "by_age2", {"age"}}});
  EXPECT_THROW(db.findByIndex("users", "by_age"), StorageError);
  EXPECT_EQ(idsOf(db.findByIndex("users", "by_age2")), std::vector<std::string>{"a"});
  EXPECT_THROW(db.createIndex("users", "by_age2", {"age"}), std::runtime_error);
  EXPECT_THROW(db.createIndex("users", "bad", {"a", "a"}), CodecError);
}

TEST(V2Attachments, RoundTripAndReferenceValidation) {
  TempDir tmp;
  auto db = Database::open((tmp.path / "db").string());
  Bytes big(2'500'000, 7);
  auto small = db.putAttachment(Bytes{'h', 'i'}, "text/plain");
  auto large = db.putAttachment(big);
  auto empty = db.putAttachment(Bytes{});
  EXPECT_EQ(db.getAttachment(small.digest), (Bytes{'h', 'i'}));
  EXPECT_EQ(db.getAttachment(large.digest), big);
  EXPECT_TRUE(db.getAttachment(empty.digest).empty());
  int chunks = 0;
  db.streamAttachment(large.digest, [&](const Bytes&) { chunks++; });
  EXPECT_EQ(chunks, 3);
  EXPECT_EQ(db.putAttachment(big).digest, large.digest);

  db.insertOne("docs", {{"_id", "ok"}, {"_files", {{"a", {{"digest", small.digest}, {"size", small.size}}}}}});
  std::string missing(128, 'a');
  EXPECT_THROW(db.insertOne("docs", {{"_id", "x"}, {"_files", {{"a", {{"digest", missing}, {"size", 1}}}}}}), AttachmentError);
  EXPECT_THROW(db.insertOne("docs", {{"_id", "x"}, {"_files", {{"a", {{"digest", small.digest}, {"size", 99}}}}}}), AttachmentError);
  EXPECT_THROW(db.insertOne("docs", {{"_id", "x"}, {"_files", {{"a", {{"digest", small.digest}, {"size", small.size}, {"extra", 1}}}}}}), AttachmentError);
  EXPECT_THROW(db.insertOne("docs", {{"_id", "x"}, {"_files", json::array()}}), AttachmentError);
  EXPECT_FALSE(db.findById("docs", "x").has_value());
}

// ---- fork and merge ----------------------------------------------------------

namespace {

std::string dumpAll(Database& db) {
  json out = json::object();
  auto names = db.listCollections();
  std::sort(names.begin(), names.end());
  for (const auto& name : names) out[name] = sortedById(json(db.findAll(name)));
  return out.dump();
}

}  // namespace

TEST(V2Merge, ForksAndMergesBothWaysToTheSameRoot) {
  TempDir tmp;
  auto a = Database::open((tmp.path / "a").string());
  a.insertMany("docs", {{{"_id", "one"}, {"v", 1}}, {{"_id", "two"}, {"v", 2}}, {{"_id", "three"}, {"v", 3}}});
  auto b = a.fork((tmp.path / "b").string());
  EXPECT_EQ(b.root(), a.root());
  EXPECT_NE(DiskStore::open((tmp.path / "b").string()).writerId(), DiskStore::open((tmp.path / "a").string()).writerId());

  a.insertOne("docs", {{"_id", "from-a"}, {"v", 10}});
  a.replaceById("docs", "one", {{"_id", "one"}, {"v", 100}});
  b.insertOne("docs", {{"_id", "from-b"}, {"v", 20}});
  b.deleteById("docs", "two");
  b.insertOne("other", {{"_id", "x"}});

  auto ab = a.merge(b);
  EXPECT_NE(ab.root, ab.previous);
  auto ba = b.merge(a);
  EXPECT_EQ(ba.root, ab.root);
  EXPECT_EQ(a.root(), b.root());
  EXPECT_EQ(dumpAll(a),
            R"({"docs":[{"_id":"from-a","v":10},{"_id":"from-b","v":20},{"_id":"one","v":100},{"_id":"three","v":3}],"other":[{"_id":"x"}]})");
  EXPECT_EQ(a.merge(b).root, ab.root);  // merging again changes nothing
}

TEST(V2Merge, GreaterVersionAndDeletionsAndGenerations) {
  TempDir tmp;
  auto a = Database::open((tmp.path / "a").string());
  a.insertMany("docs", {{{"_id", "one"}, {"v", 1}}, {{"_id", "two"}, {"v", 2}}, {{"_id", "three"}, {"v", 3}}});
  auto b = a.fork((tmp.path / "b").string());
  a.replaceById("docs", "one", {{"_id", "one"}, {"v", 11}});
  b.replaceById("docs", "one", {{"_id", "one"}, {"v", 22}});
  b.replaceById("docs", "one", {{"_id", "one"}, {"v", 23}});
  a.merge(b);
  b.merge(a);
  EXPECT_EQ(a.root(), b.root());
  EXPECT_EQ((*a.findById("docs", "one"))["v"], 23);

  auto c = a.fork((tmp.path / "c").string());
  c.deleteById("docs", "three");
  a.replaceById("docs", "two", {{"_id", "two"}, {"v", 200}});
  a.merge(c);
  EXPECT_FALSE(a.findById("docs", "three").has_value());
  c.merge(a);
  EXPECT_EQ(c.root(), a.root());

  using T = WriteOp::Type;
  auto d = a.fork((tmp.path / "d").string());
  a.writeBatch({{T::DropCollection, "docs", "", {}}, {T::Insert, "docs", "", {{"_id", "fresh"}, {"v", 1}}}});
  d.insertOne("docs", {{"_id", "stale"}, {"v", 9}});
  a.merge(d);
  d.merge(a);
  EXPECT_EQ(a.root(), d.root());
  EXPECT_EQ(dumpAll(a), R"({"docs":[{"_id":"fresh","v":1}]})");
}

TEST(V2Merge, ThreeReplicasConvergeInAnyOrder) {
  TempDir tmp;
  auto origin = Database::open((tmp.path / "o").string());
  origin.insertOne("docs", {{"_id", "seed"}});
  std::vector<Database> replicas{origin, origin.fork((tmp.path / "r1").string()), origin.fork((tmp.path / "r2").string())};
  for (std::size_t i = 0; i < replicas.size(); i++) {
    std::vector<json> docs;
    for (int k = 0; k < 5; k++) docs.push_back({{"_id", "r" + std::to_string(i) + "-" + std::to_string(k)}, {"v", int(i)}});
    replicas[i].insertMany("docs", docs);
    replicas[i].insertOne("c" + std::to_string(i), {{"_id", "only"}});
  }
  auto& x = replicas[0]; auto& y = replicas[1]; auto& z = replicas[2];
  x.merge(y); x.merge(z);
  z.merge(y); z.merge(x);
  y.merge(z); y.merge(x);
  EXPECT_EQ(x.root(), y.root());
  EXPECT_EQ(y.root(), z.root());
  EXPECT_EQ(x.findAll("docs").size(), 16u);
}

TEST(V2Merge, MergesIndexesAndAttachmentsAndRefusesBadInput) {
  TempDir tmp;
  auto a = Database::open((tmp.path / "a").string());
  a.createIndex("people", "by_city", {"city"});
  a.insertMany("people", {{{"_id", "p1"}, {"city", "Oslo"}, {"age", 30}}, {{"_id", "p2"}, {"city", "Bergen"}, {"age", 40}}});
  auto b = a.fork((tmp.path / "b").string());
  a.insertOne("people", {{"_id", "p3"}, {"city", "Oslo"}, {"age", 20}});
  a.replaceById("people", "p2", {{"_id", "p2"}, {"city", "Oslo"}, {"age", 41}});
  b.insertOne("people", {{"_id", "p4"}, {"city", "Bergen"}, {"age", 25}});
  b.createIndex("people", "by_age", {"age"});
  b.deleteById("people", "p1");
  auto ref = b.putAttachment(Bytes(1'500'000, 3), "application/octet-stream");
  b.insertOne("files", {{"_id", "f"}, {"_files", {{"x", {{"digest", ref.digest}, {"size", ref.size}, {"type", *ref.type}}}}}});

  a.merge(b);
  b.merge(a);
  EXPECT_EQ(a.root(), b.root());
  for (auto* db : {&a, &b}) {
    EXPECT_EQ(idsOf(db->findByIndex("people", "by_city", IndexQuery::fromJson({{"equals", {"Oslo"}}}))),
              (std::vector<std::string>{"p2", "p3"}));
    EXPECT_EQ(idsOf(db->findByIndex("people", "by_city", IndexQuery::fromJson({{"equals", {"Bergen"}}}))),
              (std::vector<std::string>{"p4"}));
    EXPECT_EQ(idsOf(db->findByIndex("people", "by_age", IndexQuery::fromJson({{"gte", 25}}))),
              (std::vector<std::string>{"p4", "p2"}));
  }
  EXPECT_EQ(a.getAttachment(ref.digest).size(), 1'500'000u);

  auto other = Database::open((tmp.path / "other").string());
  other.insertOne("docs", {{"_id", "x"}});
  EXPECT_THROW(a.merge(other), MergeError);
}

TEST(V2Merge, ReproducesTheTypeScriptMergedRoot) {
  auto manifest = loadJson(kFixtures / "stores" / "merged.json");
  TempDir tmp;
  auto path = [&](const char* n) { return (tmp.path / n).string(); };
  DatabaseRoot empty{manifest.at("databaseId"), 0, std::nullopt};
  DiskStore::create(path("a"), encodeMetadata(empty), manifest.at("writerA"));
  auto a = Database::open(path("a"));
  using T = WriteOp::Type;
  a.writeBatch({{T::Insert, "people", "", {{"_id", "p1"}, {"city", "Oslo"}, {"age", 30}}},
                {T::Insert, "people", "", {{"_id", "p2"}, {"city", "Bergen"}, {"age", 40}}},
                {T::CreateIndex, "people", "", {}, "by_city", {"city"}}});
  // The second replica is a copy with the fixture's writer ID.
  DiskStore::create(path("b"), encodeMetadata(empty), manifest.at("writerB"));
  {
    auto store = DiskStore::open(path("b"));
    auto head = a.root();
    store.withWriteLock([&](DiskStore::Writer& w) {
      copyDatabase(store, w, DiskStore::open(path("a")), head);
      w.publishHead(head);
    });
  }
  auto b = Database::open(path("b"));
  a.insertOne("people", {{"_id", "p3"}, {"city", "Oslo"}, {"age", 20}});
  a.replaceById("people", "p2", {{"_id", "p2"}, {"city", "Oslo"}, {"age", 41}});
  b.insertOne("people", {{"_id", "p4"}, {"city", "Bergen"}, {"age", 25}});
  b.createIndex("people", "by_age", {"age"});
  b.deleteById("people", "p1");
  EXPECT_EQ(a.root(), manifest.at("rootA").get<std::string>());
  EXPECT_EQ(b.root(), manifest.at("rootB").get<std::string>());
  EXPECT_EQ(a.merge(b).root, manifest.at("merged").get<std::string>());
  EXPECT_EQ(b.merge(a).root, manifest.at("merged").get<std::string>());
}

// ---- encrypted profile -------------------------------------------------------

namespace {

Bytes testKey(bool other = false) {
  Bytes key(32);
  for (std::size_t i = 0; i < key.size(); i++) key[i] = std::uint8_t(other ? 255 - i : i);
  return key;
}

std::vector<fs::path> filesUnder(const fs::path& root) {
  std::vector<fs::path> out;
  for (auto& e : fs::recursive_directory_iterator(root)) if (e.is_regular_file()) out.push_back(e.path());
  return out;
}

}  // namespace

TEST(V2Encryption, KnownAnswersMatchTheReference) {
  auto vectors = loadJson(kFixtures / "vectors.json").at("encryption");
  ASSERT_EQ(toHex(testKey()), vectors.at("key").get<std::string>());
  AesCipher cipher(testKey());
  for (const auto& c : vectors.at("cases")) {
    auto digest = c.at("digest").get<std::string>();
    auto plaintext = fromHex(c.at("plaintext"));
    EXPECT_EQ(digestPayload(plaintext), digest);
    EXPECT_EQ(cipher.physicalName(digest), c.at("physicalName").get<std::string>());
    auto sealed = cipher.seal(digest, plaintext, fromHex(c.at("nonce")));
    EXPECT_EQ(toHex(sealed), c.at("sealed").get<std::string>());
    EXPECT_EQ(cipher.open(digest, sealed), plaintext);
  }
  const auto& head = vectors.at("head");
  auto sealedHead = cipher.sealHead(head.at("digest"), fromHex(head.at("nonce")));
  EXPECT_EQ(toHex(sealedHead), head.at("sealed").get<std::string>());
  EXPECT_EQ(cipher.openHead(sealedHead), head.at("digest").get<std::string>());
  EXPECT_THROW(AesCipher(testKey(true)).openHead(sealedHead), DecryptionError);
  EXPECT_THROW(AesCipher(Bytes(16)), std::runtime_error);
}

TEST(V2Encryption, ReadsTheStoreWrittenByTypeScriptAndReproducesItsLogicalHeads) {
  auto manifest = loadJson(kFixtures / "stores" / "encrypted.json");
  ASSERT_EQ(manifest.at("keyHex").get<std::string>(), toHex(testKey()));
  TempDir tmp;
  fs::copy(kFixtures / "stores" / "encrypted", tmp.path / "ts", fs::copy_options::recursive);
  auto ts = Database::open((tmp.path / "ts").string(), testKey());
  EXPECT_EQ(ts.root(), manifest.at("head").get<std::string>());
  for (const auto& q : manifest.at("queries"))
    EXPECT_EQ(idsOf(ts.findByIndex(q.at("collection"), q.at("index"), IndexQuery::fromJson(q.at("query")))),
              q.at("ids").get<std::vector<std::string>>());
  EXPECT_EQ(sortedById(json(ts.findAll("people"))), manifest.at("documents"));
  EXPECT_EQ(toHex(ts.getAttachment(manifest.at("attachment").at("digest"))), toHex(attachmentBytes(false)));

  // Rebuild the same history here: the logical digests match even though nonces differ.
  auto path = (tmp.path / "cpp").string();
  DatabaseRoot empty{manifest.at("databaseId"), 0, std::nullopt};
  auto cipher = std::make_shared<const AesCipher>(testKey());
  auto store = DiskStore::create(path, encodeMetadata(empty), manifest.at("writerId"), cipher);
  auto db = Database::open(path, testKey());
  std::vector<Digest> heads{store.readHead()};
  auto mark = [&] { heads.push_back(db.root()); };
  static const char* cities[] = {"Oslo", "Bergen", "Tromso", "Stavanger"};
  using T = WriteOp::Type;
  std::vector<WriteOp> batch;
  for (int i = 0; i < 40; i++) {
    char id[8];
    std::snprintf(id, sizeof id, "p%03d", i);
    batch.push_back({T::Insert, "people", "", {{"_id", id}, {"city", cities[i % 4]}, {"age", 20 + (i % 30)}}});
  }
  batch.push_back({T::CreateIndex, "people", "", {}, "by_city_age", {"city", "age"}});
  db.writeBatch(batch);
  mark();
  auto ref = db.putAttachment(attachmentBytes(false), "text/plain");
  db.replaceById("people", "p001", {{"_id", "p001"}, {"city", "Oslo"}, {"age", 99},
                                    {"_files", {{"note", {{"digest", ref.digest}, {"size", ref.size}, {"type", "text/plain"}}}}}});
  mark();
  db.deleteById("people", "p002");
  mark();
  EXPECT_EQ(heads, manifest.at("heads").get<std::vector<std::string>>());
}

TEST(V2Encryption, StoresNothingReadableAndRejectsBadKeysAndTampering) {
  TempDir tmp;
  auto path = (tmp.path / "db").string();
  std::string secret = "a-very-distinctive-secret-string";
  {
    auto db = Database::open(path, testKey());
    db.createIndex("people", "by_city", {"city"});
    db.insertMany("people", {{{"_id", "alice"}, {"city", "Oslo"}, {"note", secret}},
                             {{"_id", "bob"}, {"city", "Bergen"}, {"note", secret}}});
  }
  EXPECT_THROW(Database::open(path), StorageError);
  EXPECT_THROW(Database::open(path, testKey(true)), StorageError);
  auto plain = (tmp.path / "plain").string();
  Database::open(plain);
  EXPECT_THROW(Database::open(plain, testKey()), StorageError);

  auto db = Database::open(path, testKey());
  auto root = db.root();
  for (const auto& file : filesUnder(path)) {
    std::ifstream in(file, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content.find(secret), std::string::npos) << file;
    EXPECT_EQ(content.find(root), std::string::npos) << file;
  }
  for (const auto& file : filesUnder(fs::path(path) / "blocks")) EXPECT_NE(file.filename().string(), root);

  // Tamper with a document block: reads fail rather than return altered data.
  std::vector<fs::path> blocks = filesUnder(fs::path(path) / "blocks");
  std::map<std::uintmax_t, std::vector<fs::path>> bySize;
  for (auto& b : blocks) bySize[fs::file_size(b)].push_back(b);
  fs::path first, second;
  for (auto& [size, files] : bySize) if (files.size() >= 2) { first = files[0]; second = files[1]; break; }
  ASSERT_FALSE(first.empty());
  fs::copy_file(second, first, fs::copy_options::overwrite_existing);  // swap a block for another
  auto swapped = Database::open(path, testKey());
  EXPECT_THROW(swapped.findAll("people"), StorageError);
}

TEST(V2Attachments, FilesOfAnySizeUseATreeOfManifestsAndStream) {
  TempDir tmp;
  auto db = Database::open((tmp.path / "db").string());
  const std::size_t total = 64 * 1'048'576ULL + 1234;  // 65 chunks: one more than a single manifest holds
  Bytes data(total);
  for (std::size_t i = 0; i < total; i++) data[i] = std::uint8_t((i / 4096 + (i >> 20)) % 251);
  std::string text(data.begin(), data.end());
  std::istringstream stream(text);
  auto ref = db.putAttachment(stream, "application/octet-stream");
  EXPECT_EQ(ref.size, total);
  auto root = decodeMetadata(DiskStore::open((tmp.path / "db").string()).readBlock(ref.digest));
  const auto* blob = std::get_if<Blob>(&root);
  ASSERT_NE(blob, nullptr);
  EXPECT_EQ(blob->depth, 1);
  EXPECT_EQ(blob->entries.size(), 2u);
  EXPECT_EQ(db.putAttachment(data).digest, ref.digest);  // same bytes, same reference
  std::size_t streamed = 0;
  db.streamAttachment(ref.digest, [&](const Bytes& c) { streamed += c.size(); });
  EXPECT_EQ(streamed, total);
  db.insertOne("docs", {{"_id", "big"}, {"_files", {{"f", {{"digest", ref.digest}, {"size", ref.size}}}}}});
  EXPECT_TRUE(db.findById("docs", "big").has_value());
}

TEST(V2Attachments, BlobManifestValidation) {
  auto digest = [](int n) { char b[3]; std::snprintf(b, sizeof b, "%02x", n); std::string d; for (int i = 0; i < 64; i++) d += b; return d; };
  auto encode = [&](std::uint64_t size, int depth, int count) {
    Blob blob;
    blob.size = size; blob.depth = depth;
    for (int i = 0; i < count; i++) blob.entries.push_back(digest(i));
    return encodeMetadata(blob);
  };
  const std::uint64_t MIB = 1'048'576;
  EXPECT_NO_THROW(decodeMetadata(encode(0, 0, 0)));
  EXPECT_NO_THROW(decodeMetadata(encode(63 * MIB, 0, 63)));
  EXPECT_NO_THROW(decodeMetadata(encode(63 * MIB + 1, 1, 2)));
  EXPECT_NO_THROW(decodeMetadata(encode(63ULL * 63 * MIB + 1, 2, 2)));
  EXPECT_THROW(encode(MIB + 1, 0, 1), CodecError);
  EXPECT_THROW(encode(64 * MIB, 0, 64), CodecError);
  // Only a file's root must be minimal; an interior manifest may hold a small tail.
  EXPECT_NO_THROW(decodeMetadata(encode(MIB, 1, 1)));
  EXPECT_THROW(encode(10, 8, 1), CodecError);
  auto bytes = encode(63 * MIB + 1, 1, 2);
  bytes.push_back(0);
  EXPECT_THROW(decodeMetadata(bytes), CodecError);
}

// ---- block sources and HTTP sync --------------------------------------------

namespace {

/// Wraps a source and counts the blocks read through it.
struct CountingSource : BlockSource {
  explicit CountingSource(const BlockSource& inner) : inner_(inner) {}
  Digest readHead() const override { heads++; return inner_.readHead(); }
  Bytes readBlock(const Digest& d) const override { blocks++; return inner_.readBlock(d); }
  mutable int heads = 0, blocks = 0;
  const BlockSource& inner_;
};

struct CorruptingSource : BlockSource {
  explicit CorruptingSource(const BlockSource& inner) : inner_(inner) {}
  Digest readHead() const override { return inner_.readHead(); }
  Bytes readBlock(const Digest& d) const override { auto b = inner_.readBlock(d); b.back() ^= 1; return b; }
  const BlockSource& inner_;
};

}  // namespace

TEST(V2Sync, TwoDatabasesConvergeOverHttpFetchingOnlyMissingBlocks) {
  TempDir tmp;
  auto a = Database::open((tmp.path / "a").string());
  std::vector<json> docs;
  for (int i = 0; i < 600; i++) docs.push_back({{"_id", "d" + std::to_string(i)}, {"v", i}});
  a.insertMany("docs", docs);
  auto b = a.fork((tmp.path / "b").string());
  auto serverA = BlockServer::start(a.blockSource());
  auto serverB = BlockServer::start(b.blockSource());

  a.insertOne("docs", {{"_id", "from-a"}, {"v", -1}});
  b.replaceById("docs", "d7", {{"_id", "d7"}, {"v", 777}});
  HttpBlockSource fromB(serverB->url());
  HttpBlockSource fromA(serverA->url());
  auto pulled = a.merge(fromB);
  b.merge(fromA);
  EXPECT_EQ(pulled.adopted, 1u);
  EXPECT_EQ(a.root(), b.root());
  EXPECT_EQ((*a.findById("docs", "d7"))["v"], 777);

  // A counting wrapper in front of the database shows what a merge reads: a few pages, not the collection.
  auto c = a.fork((tmp.path / "c").string());
  a.insertOne("docs", {{"_id", "late"}, {"v", 5}});
  auto aSource = a.blockSource();
  CountingSource counting(*aSource);
  c.merge(counting);
  EXPECT_LT(counting.blocks, 25);
  CountingSource idle(*aSource);
  EXPECT_EQ(c.merge(idle).root, c.root());
  EXPECT_EQ(idle.blocks, 0);
  EXPECT_EQ(idle.heads, 1);
}

TEST(V2Sync, TokensAndBadServers) {
  TempDir tmp;
  auto a = Database::open((tmp.path / "a").string());
  a.insertOne("docs", {{"_id", "one"}});
  auto b = a.fork((tmp.path / "b").string());
  a.insertOne("docs", {{"_id", "two"}});
  auto server = BlockServer::start(a.blockSource(), 0, "127.0.0.1", "secret");
  EXPECT_THROW(b.merge(HttpBlockSource(server->url())), StorageError);
  EXPECT_THROW(b.merge(HttpBlockSource(server->url(), "nope")), StorageError);
  b.merge(HttpBlockSource(server->url(), "secret"));
  EXPECT_TRUE(b.findById("docs", "two").has_value());

  auto c = a.fork((tmp.path / "c").string());
  a.insertOne("docs", {{"_id", "three"}});
  auto before = c.root();
  auto source = a.blockSource();
  CorruptingSource corrupt(*source);
  auto corruptServer = BlockServer::start(std::make_shared<CorruptingSource>(*source));
  EXPECT_THROW(c.merge(HttpBlockSource(corruptServer->url())), StorageError);  // digest check
  EXPECT_EQ(c.root(), before);
  EXPECT_THROW(HttpBlockSource(server->url(), "secret").readBlock(std::string(128, 'f')), MissingBlockError);
}

// ---- golden manifest trees ---------------------------------------------------

namespace {

/// A stream of `size` zero bytes.
class ZeroBuf : public std::streambuf {
 public:
  explicit ZeroBuf(std::uint64_t size) : left_(size), block_(1 << 20, 0) {}
 protected:
  int_type underflow() override {
    if (left_ == 0) return traits_type::eof();
    auto n = std::min<std::uint64_t>(left_, block_.size());
    setg(block_.data(), block_.data(), block_.data() + n);
    left_ -= n;
    return traits_type::to_int_type(block_[0]);
  }
 private:
  std::uint64_t left_;
  std::vector<char> block_;
};

}  // namespace

/// Keeps manifests in memory and remembers the digest of all-zero blocks of each length.
struct MemoryWriter : BlockWriter {
  std::map<Digest, Bytes> manifests;
  std::map<std::size_t, Digest> zeroDigests;
  Bytes zeros = Bytes(1 << 20, 0);
  Digest writeBlock(const Bytes& payload) override {
    if (std::memcmp(payload.data(), zeros.data(), payload.size()) == 0) {
      auto it = zeroDigests.find(payload.size());
      if (it == zeroDigests.end()) it = zeroDigests.emplace(payload.size(), digestPayload(payload)).first;
      return it->second;
    }
    auto digest = digestPayload(payload);
    manifests[digest] = payload;
    return digest;
  }
  void syncBlocks() override {}
};

TEST(V2Attachments, ManifestTreesMatchTheTypeScriptGoldenRoots) {
  auto vectors = loadJson(kFixtures / "vectors.json");
  for (const auto& tree : vectors.at("blobTrees")) {
    std::uint64_t size = tree.at("size").get<std::uint64_t>();
    MemoryWriter writer;
    ZeroBuf buf(size);
    std::istream in(&buf);
    auto ref = writeAttachment(writer, in, std::nullopt);
    EXPECT_EQ(ref.size, size);
    EXPECT_EQ(ref.digest, tree.at("root").get<std::string>()) << "size " << size;
    auto root = decodeMetadata(writer.manifests.at(ref.digest));
    const auto* blob = std::get_if<Blob>(&root);
    ASSERT_NE(blob, nullptr);
    EXPECT_EQ(blob->depth, tree.at("depth").get<int>()) << "size " << size;
    EXPECT_EQ(blob->size, size);
    // The Bytes-based writer builds the same tree.
    if (size <= 130ULL * 1048576) {
      MemoryWriter other;
      EXPECT_EQ(writeAttachment(other, Bytes(size, 0), std::nullopt).digest, ref.digest);
    }
  }
}

TEST(V2Attachments, DeepTreeStreamsAndRejectsAnOverDeepRoot) {
  TempDir tmp;
  auto db = Database::open((tmp.path / "db").string());
  // 65 distinct chunks: chunk order must be file order.
  std::string text;
  for (int chunk = 0; chunk < 65; chunk++) {
    std::string part(1 << 20, '\0');
    part[0] = char(chunk + 1);
    text += part;
  }
  std::istringstream in(text);
  auto ref = db.putAttachment(in);
  int index = 0;
  db.streamAttachment(ref.digest, [&](const Bytes& chunk) {
    index++;
    EXPECT_EQ(chunk[0], index);
  });
  EXPECT_EQ(index, 65);

  // A depth 1 root that fits one child is deeper than its size needs; readers refuse it.
  auto store = DiskStore::open((tmp.path / "db").string());
  auto small = db.putAttachment(Bytes{'x'});
  Blob over;
  over.size = 1;
  over.depth = 1;
  over.entries = {small.digest};
  Digest overDigest;
  db.putAttachment(Bytes{});  // keep the lock path exercised
  DiskStore writerStore = DiskStore::open((tmp.path / "db").string());
  writerStore.withWriteLock([&](DiskStore::Writer& w) { overDigest = w.writeBlock(encodeMetadata(over)); w.syncBlocks(); });
  EXPECT_THROW(db.getAttachment(overDigest), AttachmentError);
  EXPECT_THROW(db.insertOne("docs", {{"_id", "x"}, {"_files", {{"f", {{"digest", overDigest}, {"size", 1}}}}}}), AttachmentError);
}
