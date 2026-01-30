#pragma once

#include <brightchain/base_block.hpp>
#include <brightchain/checksum.hpp>
#include <vector>

namespace brightchain {

/**
 * RawDataBlock represents a block containing raw, unencrypted data.
 * No header, just pure data storage with checksum validation.
 */
class RawDataBlock : public BaseBlock {
public:
    RawDataBlock(BlockSize blockSize, const std::vector<uint8_t>& data,
                 const Checksum& checksum);

    void validateSync() const override;
    const std::vector<uint8_t>& data() const override { return data_; }
    std::vector<uint8_t> layerHeaderData() const override { return {}; }
    std::vector<uint8_t> layerPayload() const override { return data_; }
    size_t layerOverheadSize() const override { return 0; }

private:
    std::vector<uint8_t> data_;
};

} // namespace brightchain
