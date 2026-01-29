#include <brightchain/block_metadata.hpp>

namespace brightchain {

nlohmann::json BlockMetadata::to_json() const {
    auto time_t_val = std::chrono::system_clock::to_time_t(created_at);
    return {
        {"size", static_cast<int>(size)},
        {"created_at", time_t_val},
        {"length_without_padding", length_without_padding}
    };
}

BlockMetadata BlockMetadata::from_json(const nlohmann::json& j) {
    auto size = static_cast<BlockSize>(j["size"].get<int>());
    auto len = j["length_without_padding"].get<size_t>();
    auto time_t_val = j["created_at"].get<std::time_t>();
    auto created = std::chrono::system_clock::from_time_t(time_t_val);
    return BlockMetadata(size, len, created);
}

} // namespace brightchain
