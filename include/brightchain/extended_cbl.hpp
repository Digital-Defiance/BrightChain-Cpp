#pragma once

#include <brightchain/cbl.hpp>
#include <string>

namespace brightchain {

/**
 * Extended CBL Header Structure (after base CBL header):
 * [FileNameLength(1)][FileName(variable)][MimeTypeLength(1)][MimeType(variable)]
 */
struct ExtendedCBLMetadata {
    std::string fileName;
    std::string mimeType;

    std::vector<uint8_t> serialize() const;
    static ExtendedCBLMetadata deserialize(const std::vector<uint8_t>& data, size_t offset);
    size_t size() const;
};

/**
 * Extended CBL - CBL with file name and MIME type metadata.
 * Structure: [CBL Header][File Metadata][Block References][Padding]
 */
class ExtendedCBL : public ConstituentBlockListBlock {
public:
    ExtendedCBL(BlockSize blockSize, const std::vector<uint8_t>& data,
                const Checksum& checksum);

    void validateSync() const override;
    std::vector<uint8_t> layerHeaderData() const override;
    size_t layerOverheadSize() const override;

    const std::string& fileName() const { return metadata_.fileName; }
    const std::string& mimeType() const { return metadata_.mimeType; }

private:
    ExtendedCBLMetadata metadata_;
};

} // namespace brightchain
