#include <brightchain/raw_data_block.hpp>
#include <brightchain/cbl.hpp>
#include <brightchain/extended_cbl.hpp>
#include <iostream>

using namespace brightchain;

int main() {
    std::cout << "BrightChain Block Types Example\n\n";

    // 1. RawDataBlock - Simple data storage
    std::cout << "1. Creating RawDataBlock...\n";
    std::vector<uint8_t> data = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; // "Hello"
    auto checksum = Checksum::fromData(data);
    RawDataBlock rawBlock(BlockSize::Message, data, checksum);
    std::cout << "   Block size: " << static_cast<uint32_t>(rawBlock.blockSize()) << " bytes\n";
    std::cout << "   Data length: " << rawBlock.data().size() << " bytes\n";
    std::cout << "   Checksum: " << rawBlock.idChecksum().toHex().substr(0, 16) << "...\n\n";

    // 2. CBL - Constituent Block List
    std::cout << "2. Creating CBL Block...\n";
    CBLHeader cblHeader;
    cblHeader.magic = BlockHeaderConstants::MAGIC_PREFIX;
    cblHeader.version = BlockHeaderConstants::VERSION;
    cblHeader.type = static_cast<uint8_t>(StructuredBlockType::CBL);
    cblHeader.creatorId.fill(0x42);
    cblHeader.dateCreated = std::time(nullptr);
    cblHeader.addressCount = 3;
    cblHeader.tupleSize = 3;
    cblHeader.originalDataLength = 3072;
    cblHeader.originalDataChecksum.fill(0xAB);
    cblHeader.signature.fill(0xCD);

    auto cblData = cblHeader.serialize();
    
    // Add three block references
    for (int i = 0; i < 3; ++i) {
        Checksum::HashArray hash;
        hash.fill(0x10 + i);
        cblData.insert(cblData.end(), hash.begin(), hash.end());
    }

    auto cblChecksum = Checksum::fromData(cblData);
    ConstituentBlockListBlock cbl(BlockSize::Small, cblData, cblChecksum);
    
    std::cout << "   Address count: " << cbl.addressCount() << "\n";
    std::cout << "   Tuple size: " << cbl.tupleSize() << "\n";
    std::cout << "   Original data length: " << cbl.originalDataLength() << " bytes\n";
    std::cout << "   Block references:\n";
    
    auto addresses = cbl.addresses();
    for (size_t i = 0; i < addresses.size(); ++i) {
        std::cout << "     [" << i << "] " << addresses[i].toHex().substr(0, 16) << "...\n";
    }
    std::cout << "\n";

    // 3. ExtendedCBL - CBL with file metadata
    std::cout << "3. ExtendedCBL Block (skipped - complex manual construction)\n";
    std::cout << "   Note: ExtendedCBL requires metadata embedded in header structure\n";
    std::cout << "   See ExtendedCBL tests for proper construction examples\n\n";

    std::cout << "Block types demonstration complete!\n";
    
    return 0;
}
