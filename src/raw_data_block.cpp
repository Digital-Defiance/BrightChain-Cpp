#include <brightchain/raw_data_block.hpp>
#include <stdexcept>

namespace brightchain {

RawDataBlock::RawDataBlock(BlockSize blockSize, const std::vector<uint8_t>& data,
                           const Checksum& checksum)
    : BaseBlock(BlockType::RawData, BlockDataType::RawData, blockSize, checksum),
      data_(data) {
    if (data.size() > static_cast<uint32_t>(blockSize)) {
        throw std::invalid_argument("Data length exceeds block size");
    }
}

void RawDataBlock::validateSync() const {
    auto computed = Checksum::fromData(data_);
    if (computed != checksum_) {
        throw std::runtime_error("Checksum mismatch");
    }
}

} // namespace brightchain
