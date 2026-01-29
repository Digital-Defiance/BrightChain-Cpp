#include "brightchain/hmac_drbg.hpp"
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace brightchain {

HMAC_DRBG::HMAC_DRBG(const std::vector<uint8_t>& seed) {
    v_.resize(64, 0x01);
    k_.resize(64, 0x00);
    update(seed);
}

std::vector<uint8_t> HMAC_DRBG::generate(size_t num_bytes) {
    std::vector<uint8_t> result;
    while (result.size() < num_bytes) {
        hmac(k_, v_, v_);
        result.insert(result.end(), v_.begin(), v_.end());
    }
    result.resize(num_bytes);
    update({});
    return result;
}

void HMAC_DRBG::update(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> temp;
    temp.insert(temp.end(), v_.begin(), v_.end());
    temp.push_back(0x00);
    if (!data.empty()) {
        temp.insert(temp.end(), data.begin(), data.end());
    }
    hmac(k_, temp, k_);
    hmac(k_, v_, v_);
    
    if (!data.empty()) {
        temp.clear();
        temp.insert(temp.end(), v_.begin(), v_.end());
        temp.push_back(0x01);
        temp.insert(temp.end(), data.begin(), data.end());
        hmac(k_, temp, k_);
        hmac(k_, v_, v_);
    }
}

void HMAC_DRBG::hmac(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data, 
          std::vector<uint8_t>& out) {
    unsigned int len = 64;
    out.resize(len);
    HMAC(EVP_sha512(), key.data(), key.size(), data.data(), data.size(), 
         out.data(), &len);
}

} // namespace brightchain
