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
    std::cout << "3. Creating ExtendedCBL Block...\n";
    CBLHeader ecblHeader;
    ecblHeader.magic = BlockHeaderConstants::MAGIC_PREFIX;
    ecblHeader.version = BlockHeaderConstants::VERSION;
    ecblHeader.type = static_cast<uint8_t>(StructuredBlockType::ExtendedCBL);
    ecblHeader.creatorId.fill(0x99);
    ecblHeader.dateCreated = std::time(nullptr);
    ecblHeader.addressCount = 2;
    ecblHeader.tupleSize = 2;
    ecblHeader.originalDataLength = 2048;
    ecblHeader.originalDataChecksum.fill(0xEF);
    ecblHeader.signature.fill(0xFE);

    auto ecblData = ecblHeader.serialize();
    
    // Add file metadata
    ExtendedCBLMetadata metadata;
    metadata.fileName = "example.pdf";
    metadata.mimeType = "application/pdf";
    auto metadataBytes = metadata.serialize();
    ecblData.insert(ecblData.end(), metadataBytes.begin(), metadataBytes.end());
    
    // Add two block references
    for (int i = 0; i < 2; ++i) {
        Checksum::HashArray hash;
        hash.fill(0x20 + i);
        ecblData.insert(ecblData.end(), hash.begin(), hash.end());
    }

    auto ecblChecksum = Checksum::fromData(ecblData);
    ExtendedCBL ecbl(BlockSize::Small, ecblData, ecblChecksum);
    
    std::cout << "   File name: " << ecbl.fileName() << "\n";
    std::cout << "   MIME type: " << ecbl.mimeType() << "\n";
    std::cout << "   Address count: " << ecbl.addressCount() << "\n";
    std::cout << "   Original data length: " << ecbl.originalDataLength() << " bytes\n";
    std::cout << "   Header overhead: " << ecbl.layerOverheadSize() << " bytes\n\n";

    std::cout << "All block types created and validated successfully!\n";
    
    return 0;
}
