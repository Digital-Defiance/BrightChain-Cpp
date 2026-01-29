#include <iostream>
#include "include/brightchain/block_size.hpp"

int main() {
    using namespace brightchain;
    std::cout << "VALID_BLOCK_SIZES order:" << std::endl;
    for (const auto& size : VALID_BLOCK_SIZES) {
        std::cout << "  " << static_cast<uint32_t>(size) << std::endl;
    }
    return 0;
}
