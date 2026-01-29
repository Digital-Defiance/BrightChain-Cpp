#include <iostream>
#include <vector>
#include "brightchain/disk_block_store.hpp"
#include "brightchain/block_size.hpp"

int main() {
    try {
        // Create a block store
        brightchain::DiskBlockStore store("./brightchain_data", brightchain::BlockSize::Medium);
        
        std::cout << "BrightChain Block Storage Example\n";
        std::cout << "==================================\n\n";
        
        // Create some sample data
        std::string message = "Hello, BrightChain! This is a test block.";
        std::vector<uint8_t> data(message.begin(), message.end());
        
        std::cout << "Storing block with " << data.size() << " bytes...\n";
        
        // Store the block
        auto checksum = store.put(data);
        std::cout << "Block stored with checksum: " << checksum.toHex() << "\n\n";
        
        // Check if block exists
        if (store.has(checksum)) {
            std::cout << "Block exists in store\n";
        }
        
        // Retrieve the block
        std::cout << "Retrieving block...\n";
        auto retrieved = store.get(checksum);
        
        std::string retrievedMessage(retrieved.begin(), retrieved.end());
        std::cout << "Retrieved data: " << retrievedMessage << "\n\n";
        
        // Verify data integrity
        if (data == retrieved) {
            std::cout << "✓ Data integrity verified!\n";
        } else {
            std::cout << "✗ Data integrity check failed!\n";
        }
        
        std::cout << "\nBlock size: " << brightchain::blockSizeToString(store.blockSize()) << "\n";
        std::cout << "Store path: " << store.storePath() << "\n";
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
