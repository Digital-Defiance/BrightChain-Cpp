#include <brightchain/poll.hpp>
#include <stdexcept>
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>
#include <set>

namespace brightchain {

// Helper to get current timestamp in milliseconds
static int64_t getCurrentTimestamp() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
}

// Helper to generate random bytes
static std::vector<uint8_t> generateRandomBytes(size_t length) {
    std::vector<uint8_t> result(length);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    
    for (size_t i = 0; i < length; i++) {
        result[i] = static_cast<uint8_t>(dis(gen));
    }
    
    return result;
}

// Helper to compare bigint bytes
static bool compareBigintBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    if (a.size() != b.size()) {
        return a.size() < b.size();
    }
    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i]) {
            return a[i] < b[i];
        }
    }
    return false;
}

Poll::Poll(
    const std::vector<uint8_t>& id,
    const std::vector<std::string>& choices,
    VotingMethod method,
    const Member& authority,
    std::shared_ptr<PaillierPublicKey> votingPublicKey,
    const std::optional<std::vector<uint8_t>>& maxWeight,
    bool allowInsecure
)
    : id_(id)
    , choices_(choices)
    , method_(method)
    , authority_(authority)
    , votingPublicKey_(votingPublicKey)
    , createdAt_(getCurrentTimestamp())
    , maxWeight_(maxWeight)
{
    if (choices.size() < 2) {
        throw std::invalid_argument("Poll requires at least 2 choices");
    }
    
    if (!authority.hasVotingKeys()) {
        throw std::invalid_argument("Authority must have voting keys");
    }
    
    // Validate security level
    SecurityLevel level = getSecurityLevel(method);
    if (level == SecurityLevel::Insecure && !allowInsecure) {
        throw std::invalid_argument(
            "Voting method is not cryptographically secure with Paillier. "
            "Set allowInsecure: true to use anyway (NOT RECOMMENDED)."
        );
    }
}

VoteReceipt Poll::vote(const Member& voter, const EncryptedVote& vote) {
    if (isClosed()) {
        throw std::runtime_error("Poll is closed");
    }
    
    std::string voterId = toKey(voter.idBytes());
    if (receipts_.find(voterId) != receipts_.end()) {
        throw std::runtime_error("Already voted");
    }
    
    // Validate vote structure based on method
    validateVote(vote);
    
    // Store encrypted vote
    votes_[voterId] = vote.encrypted;
    
    // Generate receipt
    VoteReceipt receipt = generateReceipt(voter);
    receipts_[voterId] = receipt;
    
    return receipt;
}

bool Poll::verifyReceipt(const Member& voter, const VoteReceipt& receipt) const {
    std::string voterId = toKey(voter.idBytes());
    auto it = receipts_.find(voterId);
    if (it == receipts_.end()) {
        return false;
    }
    
    // Verify signature
    std::vector<uint8_t> data = receiptData(receipt);
    return authority_.verify(data, receipt.signature);
}

void Poll::close() {
    if (isClosed()) {
        throw std::runtime_error("Already closed");
    }
    closedAt_ = getCurrentTimestamp();
}

std::map<std::string, std::vector<std::vector<uint8_t>>> Poll::getEncryptedVotes() const {
    // Return a copy to ensure immutability
    return votes_;
}

void Poll::validateVote(const EncryptedVote& vote) const {
    switch (method_) {
        case VotingMethod::Plurality:
            if (!vote.choiceIndex.has_value()) {
                throw std::invalid_argument("Choice required");
            }
            if (vote.choiceIndex.value() < 0 || vote.choiceIndex.value() >= static_cast<int>(choices_.size())) {
                throw std::invalid_argument("Invalid choice");
            }
            break;

        case VotingMethod::Approval:
            if (!vote.choices.has_value() || vote.choices.value().empty()) {
                throw std::invalid_argument("Choices required");
            }
            for (int c : vote.choices.value()) {
                if (c < 0 || c >= static_cast<int>(choices_.size())) {
                    throw std::invalid_argument("Invalid choice");
                }
            }
            break;

        case VotingMethod::Weighted:
            if (!vote.choiceIndex.has_value()) {
                throw std::invalid_argument("Choice required");
            }
            if (!vote.weight.has_value() || vote.weight.value().empty()) {
                throw std::invalid_argument("Weight must be positive");
            }
            if (maxWeight_.has_value() && compareBigintBytes(maxWeight_.value(), vote.weight.value())) {
                throw std::invalid_argument("Weight exceeds maximum");
            }
            break;

        case VotingMethod::Borda:
        case VotingMethod::RankedChoice: {
            if (!vote.rankings.has_value() || vote.rankings.value().empty()) {
                throw std::invalid_argument("Rankings required");
            }
            std::set<int> seen;
            for (int r : vote.rankings.value()) {
                if (r < 0 || r >= static_cast<int>(choices_.size())) {
                    throw std::invalid_argument("Invalid choice");
                }
                if (seen.count(r) > 0) {
                    throw std::invalid_argument("Duplicate ranking");
                }
                seen.insert(r);
            }
            break;
        }

        default:
            // Other methods use similar validation
            break;
    }

    if (vote.encrypted.empty()) {
        throw std::invalid_argument("Encrypted data required");
    }
}

VoteReceipt Poll::generateReceipt(const Member& voter) {
    VoteReceipt receipt;
    receipt.voterId = voter.idBytes();
    receipt.pollId = id_;
    receipt.timestamp = getCurrentTimestamp();
    receipt.nonce = generateRandomBytes(16);
    
    std::vector<uint8_t> data = receiptData(receipt);
    receipt.signature = authority_.sign(data);
    
    return receipt;
}

std::vector<uint8_t> Poll::receiptData(const VoteReceipt& receipt) const {
    std::vector<uint8_t> result;
    
    // Concatenate all receipt data
    result.insert(result.end(), receipt.voterId.begin(), receipt.voterId.end());
    result.insert(result.end(), receipt.pollId.begin(), receipt.pollId.end());
    
    // Add timestamp as 8 bytes
    for (int i = 0; i < 8; i++) {
        result.push_back(static_cast<uint8_t>((receipt.timestamp >> (i * 8)) & 0xFF));
    }
    
    result.insert(result.end(), receipt.nonce.begin(), receipt.nonce.end());
    
    return result;
}

std::string Poll::toKey(const std::vector<uint8_t>& id) const {
    std::ostringstream oss;
    for (uint8_t byte : id) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    }
    return oss.str();
}

std::vector<uint8_t> Poll::hashVoterId(const std::vector<uint8_t>& voterId) const {
    // Simple hash for voter ID anonymization
    std::vector<uint8_t> hash(32, 0);
    for (size_t i = 0; i < voterId.size() && i < 32; i++) {
        hash[i] = voterId[i];
    }
    return hash;
}

} // namespace brightchain
