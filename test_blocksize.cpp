#include <iostream>
#include "include/brightchain/block_size.hpp"

int main() {
    using namespace brightchain;
    std::cout << "Small = " << static_cast<uint32_t>(BlockSize::Small) << std::endl;
    std::cout << "lengthToClosestBlockSize(5000) = " << static_cast<uint32_t>(lengthToClosestBlockSize(5000)) << std::endl;
    return 0;
}
