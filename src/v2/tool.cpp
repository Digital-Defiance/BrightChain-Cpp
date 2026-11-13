// Usage: brightdb_v2_tool <write|delete|dump|loop> <directory> [args...]
// Mirrors brightchain-db/test-support/v2-worker.ts. Prints one JSON line.
#include <algorithm>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <iterator>

#include "brightchain/v2/database.hpp"

using namespace brightchain::v2;
using nlohmann::json;

int main(int argc, char** argv) {
  try {
    if (argc < 3) throw std::runtime_error("usage: brightdb_v2_tool <command> <directory> [args]");
    std::string command = argv[1];
    // BRIGHTDB_KEY (64 hex digits) opens an encrypted database.
    std::optional<Bytes> key;
    if (const char* hex = std::getenv("BRIGHTDB_KEY")) key = fromHex(hex);
    if (command == "clone" && argc >= 4) {
      // clone <target directory> <url> [token]: copy a served database into a new directory
      HttpBlockSource source(argv[3], argc >= 5 ? std::optional<std::string>(argv[4]) : std::nullopt);
      Database::cloneFrom(argv[2], source, key);
      std::cout << R"({"ok":true})" << std::endl;
      return 0;
    }
    auto db = Database::open(argv[2], key);
    if (command == "write" && argc == 6) {
      // write <collection> <tag> <count>: one atomic batch
      std::string collection = argv[3], tag = argv[4];
      int count = std::stoi(argv[5]);
      std::vector<WriteOp> ops;
      for (int i = 0; i < count; i++) {
        ops.push_back({WriteOp::Type::Insert, collection, "",
                       json{{"_id", tag + "-" + std::to_string(i)}, {"tag", tag}, {"index", i}}});
      }
      db.writeBatch(ops);
      std::cout << R"({"ok":true})" << std::endl;
    } else if (command == "delete" && argc == 5) {
      std::cout << json{{"acknowledged", true}, {"deletedCount", db.deleteById(argv[3], argv[4])}}.dump() << std::endl;
    } else if (command == "dump") {
      json out = json::object();
      auto names = db.listCollections();
      for (const auto& name : names) out[name] = db.findAll(name);
      std::cout << out.dump() << std::endl;
    } else if (command == "insertjson" && argc == 5) {
      // insertjson <collection> <json>
      std::cout << json{{"insertedId", db.insertOne(argv[3], json::parse(argv[4]))}}.dump() << std::endl;
    } else if (command == "createindex" && argc >= 6) {
      // createindex <collection> <name> <field>...
      db.createIndex(argv[3], argv[4], std::vector<std::string>(argv + 5, argv + argc));
      std::cout << R"({"ok":true})" << std::endl;
    } else if (command == "findindex" && argc == 6) {
      // findindex <collection> <index> <query json>: ids in index order
      std::vector<std::string> ids;
      for (const auto& d : db.findByIndex(argv[3], argv[4], IndexQuery::fromJson(json::parse(argv[5]))))
        ids.push_back(d.at("_id").get<std::string>());
      std::cout << json(ids).dump() << std::endl;
    } else if (command == "putfile" && argc >= 4) {
      // putfile <path> [type]: prints the reference to put under _files
      std::ifstream in(argv[3], std::ios::binary);
      Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
      auto ref = db.putAttachment(bytes, argc >= 5 ? std::optional<std::string>(argv[4]) : std::nullopt);
      json out{{"digest", ref.digest}, {"size", ref.size}};
      if (ref.type) out["type"] = *ref.type;
      std::cout << out.dump() << std::endl;
    } else if (command == "getfile" && argc == 4) {
      // getfile <manifest digest>: prints the size and the SHA3-512 of the content
      auto bytes = db.getAttachment(argv[3]);
      std::cout << json{{"size", bytes.size()}, {"sha3", digestPayload(bytes)}}.dump() << std::endl;
    } else if (command == "fork" && argc == 4) {
      // fork <target directory>: an independent replica with a new writer ID
      db.fork(argv[3]);
      std::cout << R"({"ok":true})" << std::endl;
    } else if (command == "merge" && argc >= 4) {
      // merge <other directory> [root]: merge the other replica into this one
      auto other = Database::open(argv[3], key);
      auto result = db.merge(other, argc >= 5 ? std::optional<std::string>(argv[4]) : std::nullopt);
      std::cout << json{{"previous", result.previous}, {"root", result.root}, {"adopted", result.adopted}}.dump() << std::endl;
    } else if (command == "serve" && argc >= 3) {
      // serve [port] [token]: serve this database read-only until the process is stopped
      unsigned short port = argc >= 4 ? static_cast<unsigned short>(std::stoi(argv[3])) : 0;
      auto server = BlockServer::start(db.blockSource(), port, "127.0.0.1",
                                       argc >= 5 ? std::optional<std::string>(argv[4]) : std::nullopt,
                                       argc >= 6 ? std::optional<std::string>(argv[5]) : std::nullopt);
      std::cout << json{{"url", server->url()}}.dump() << std::endl;
      std::string line;
      while (std::getline(std::cin, line)) {}  // run until stdin closes
      return 0;
    } else if (command == "pull" && argc >= 4) {
      // pull <url> [token]: merge a served replica into this one
      HttpBlockSource source(argv[3], argc >= 5 ? std::optional<std::string>(argv[4]) : std::nullopt);
      auto result = db.merge(source);
      std::cout << json{{"previous", result.previous}, {"root", result.root}, {"adopted", result.adopted}}.dump() << std::endl;
    } else if (command == "clone" && argc >= 4) {
      // clone <url> [token]: handled before the database is opened (see below)
      throw std::runtime_error("clone is handled before open");
    } else if (command == "find" && argc == 5) {
      // find <collection> <filter json>: ids of matching documents, in key order
      std::vector<std::string> ids;
      for (const auto& d : db.find(argv[3], json::parse(argv[4]))) ids.push_back(d.at("_id").get<std::string>());
      std::sort(ids.begin(), ids.end());
      std::cout << json(ids).dump() << std::endl;
    } else if (command == "gc" && argc >= 3) {
      // gc [graceMs]: delete blocks the current root does not reach
      GcOptions options;
      options.graceMs = argc >= 4 ? std::stoll(argv[3]) : 3'600'000;
      auto r = db.collectGarbage(options);
      std::cout << json{{"keptReachable", r.keptReachable}, {"keptRecent", r.keptRecent}, {"removed", r.removed}}.dump() << std::endl;
    } else if (command == "root" && argc == 3) {
      std::cout << json{{"root", db.root()}}.dump() << std::endl;
    } else if (command == "diff" && argc == 5) {
      // diff <from> <to>: sorted "collection/id:type" summaries
      std::vector<std::string> lines;
      for (const auto& c : db.diff(argv[3], argv[4])) {
        const char* type = c.type == Change::Type::Added ? "added" : c.type == Change::Type::Modified ? "modified" : "deleted";
        lines.push_back(c.collection + "/" + c.id + ":" + type);
      }
      std::sort(lines.begin(), lines.end());
      std::cout << json(lines).dump() << std::endl;
    } else if (command == "loop" && argc == 5) {
      std::string collection = argv[3];
      int size = std::stoi(argv[4]);
      std::cout << R"({"ready":true})" << std::endl;
      for (int batch = 0;; batch++) {
        std::vector<WriteOp> ops;
        for (int i = 0; i < size; i++) {
          ops.push_back({WriteOp::Type::Insert, collection, "",
                         json{{"_id", "b" + std::to_string(batch) + "-" + std::to_string(i)},
                              {"batch", batch}, {"size", size}}});
        }
        db.writeBatch(ops);
      }
    } else {
      throw std::runtime_error("unknown command or wrong arguments");
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return 1;
  }
}
