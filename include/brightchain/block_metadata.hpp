#pragma once

#include <brightchain/block_size.hpp>
#include <chrono>
#include <nlohmann/json.hpp>
#include <string>

namespace brightchain {

struct BlockMetadata {
    BlockSize size;
    std::chrono::system_clock::time_point created_at;
    size_t length_without_padding;
    
    BlockMetadata(BlockSize s, size_t len)
        : size(s), created_at(std::chrono::system_clock::now()), length_without_padding(len) {}
    
    BlockMetadata(BlockSize s, size_t len, std::chrono::system_clock::time_point created)
        : size(s), created_at(created), length_without_padding(len) {}
    
    nlohmann::json to_json() const;
    static BlockMetadata from_json(const nlohmann::json& j);
};

} // namespace brightchain
