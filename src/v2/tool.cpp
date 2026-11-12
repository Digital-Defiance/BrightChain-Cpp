// Usage: brightdb_v2_tool <write|delete|dump|loop> <directory> [args...]
// Mirrors brightchain-db/test-support/v2-worker.ts. Prints one JSON line.
#include <iostream>

#include "brightchain/v2/database.hpp"

using namespace brightchain::v2;
using nlohmann::json;

int main(int argc, char** argv) {
  try {
    if (argc < 3) throw std::runtime_error("usage: brightdb_v2_tool <command> <directory> [args]");
    std::string command = argv[1];
    auto db = Database::open(argv[2]);
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
