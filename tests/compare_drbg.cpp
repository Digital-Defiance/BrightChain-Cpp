#include <brightchain/paillier.hpp>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        bytes.push_back(static_cast<uint8_t>(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16)));
    }
    return bytes;
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

int main() {
    std::cout << "🔍 HMAC-DRBG Output Comparison (C++)\n\n";
    
    // Load TypeScript outputs
    std::ifstream f("test_vectors_drbg_ts.json");
    if (!f.is_open()) {
        std::cerr << "❌ TypeScript vectors not found. Run: cd ecies-lib && npx tsx compare-drbg.ts\n";
        return 1;
    }
    
    json ts_vectors = json::parse(f);
    auto seed = hex_to_bytes(ts_vectors["seed"].get<std::string>());
    
    std::cout << "Seed (64 bytes):\n  " << ts_vectors["seed"].get<std::string>() << "\n\n";
    
    // Create C++ DRBG
    brightchain::HMAC_DRBG drbg(seed);
    
    std::cout << "Comparing first 10 DRBG outputs (192 bytes each):\n\n";
    
    bool all_match = true;
    for (int i = 0; i < 10; i++) {
        auto cpp_bytes = drbg.generate(192);
        auto cpp_hex = bytes_to_hex(cpp_bytes);
        auto ts_hex = ts_vectors["outputs"][i].get<std::string>();
        
        bool match = (cpp_hex == ts_hex);
        all_match = all_match && match;
        
        std::cout << "Output " << i << ": " << (match ? "✅ MATCH" : "❌ DIFFER") << "\n";
        if (!match) {
            std::cout << "  TS:  " << ts_hex.substr(0, 64) << "...\n";
            std::cout << "  C++: " << cpp_hex.substr(0, 64) << "...\n";
            
            // Find first difference
            for (size_t j = 0; j < std::min(ts_hex.length(), cpp_hex.length()); j++) {
                if (ts_hex[j] != cpp_hex[j]) {
                    std::cout << "  First diff at position " << j << ": TS='" << ts_hex[j] 
                              << "' C++='" << cpp_hex[j] << "'\n";
                    break;
                }
            }
        }
    }
    
    std::cout << "\n" << std::string(60, '=') << "\n";
    if (all_match) {
        std::cout << "✅ SUCCESS: All DRBG outputs match!\n";
        std::cout << "   HMAC-DRBG is producing identical byte sequences\n";
        return 0;
    } else {
        std::cout << "❌ FAILURE: DRBG outputs differ!\n";
        std::cout << "   This explains why primes are different\n";
        return 1;
    }
}
