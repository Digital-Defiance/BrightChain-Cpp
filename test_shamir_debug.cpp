#include <iostream>
#include "include/brightchain/shamir.hpp"

int main() {
    using namespace brightchain;
    ShamirSecretSharing shamir(8);
    
    std::string secret = "deadbeef";
    auto shares = shamir.share(secret, 5, 3);
    
    std::cout << "Shares:" << std::endl;
    for (size_t i = 0; i < shares.size(); ++i) {
        std::cout << i << ": " << shares[i] << std::endl;
    }
    
    return 0;
}
