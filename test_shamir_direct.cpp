#include <iostream>
#include "include/brightchain/shamir.hpp"
using namespace brightchain;

int main() {
    ShamirSecretSharing shamir(8);
    
    // Use the C++ generated shares  
    std::vector<std::string> shares = {
        "8019393d8c7b4aa5f35cc1a09ffd8f4cac0",
        "802a90032542db06129241803bb05ee6ba7",
        "8033a93ea93991a3e1ce8020a4403b71f88"
    };
    
    std::string recovered = shamir.combine(shares);
    std::cout << "Recovered: " << recovered << std::endl;
    std::cout << "Expected: deadbeef" << std::endl;
    
    return 0;
}
