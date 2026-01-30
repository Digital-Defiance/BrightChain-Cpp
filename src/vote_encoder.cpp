#include <brightchain/vote_encoder.hpp>
#include <stdexcept>
#include <set>

namespace brightchain {

// Helper to convert bigint (as bytes) to/from integer
static std::vector<uint8_t> intToBytes(int64_t value) {
    std::vector<uint8_t> result;
    if (value == 0) {
        result.push_back(0);
        return result;
    }
    
    bool negative = value < 0;
    uint64_t absValue = negative ? -value : value;
    
    while (absValue > 0) {
        result.push_back(static_cast<uint8_t>(absValue & 0xFF));
        absValue >>= 8;
    }
    
    // Add sign byte if needed
    if (negative) {
        result.push_back(0xFF);
    }
    
    return result;
}

VoteEncoder::VoteEncoder(std::shared_ptr<PaillierPublicKey> votingPublicKey)
    : votingPublicKey_(votingPublicKey) {
    if (!votingPublicKey) {
        throw std::invalid_argument("Voting public key cannot be null");
    }
}

EncryptedVote VoteEncoder::encodePlurality(int choiceIndex, int choiceCount) const {
    EncryptedVote vote;
    vote.choiceIndex = choiceIndex;
    
    for (int i = 0; i < choiceCount; i++) {
        // Only encrypt 1 for selected choice, 0 for others
        std::vector<uint8_t> plaintext = intToBytes(i == choiceIndex ? 1 : 0);
        vote.encrypted.push_back(votingPublicKey_->encrypt(plaintext));
    }
    
    return vote;
}

EncryptedVote VoteEncoder::encodeApproval(const std::vector<int>& choices, int choiceCount) const {
    std::set<int> choiceSet(choices.begin(), choices.end());
    EncryptedVote vote;
    vote.choices = choices;
    
    for (int i = 0; i < choiceCount; i++) {
        std::vector<uint8_t> plaintext = intToBytes(choiceSet.count(i) > 0 ? 1 : 0);
        vote.encrypted.push_back(votingPublicKey_->encrypt(plaintext));
    }
    
    return vote;
}

EncryptedVote VoteEncoder::encodeWeighted(int choiceIndex, const std::vector<uint8_t>& weight, int choiceCount) const {
    EncryptedVote vote;
    vote.choiceIndex = choiceIndex;
    vote.weight = weight;
    
    std::vector<uint8_t> zero = intToBytes(0);
    
    for (int i = 0; i < choiceCount; i++) {
        if (i == choiceIndex) {
            vote.encrypted.push_back(votingPublicKey_->encrypt(weight));
        } else {
            vote.encrypted.push_back(votingPublicKey_->encrypt(zero));
        }
    }
    
    return vote;
}

EncryptedVote VoteEncoder::encodeBorda(const std::vector<int>& rankings, int choiceCount) const {
    EncryptedVote vote;
    vote.rankings = rankings;
    
    std::vector<uint8_t> zero = intToBytes(0);
    int points = static_cast<int>(rankings.size());
    
    // Initialize all to 0
    for (int i = 0; i < choiceCount; i++) {
        vote.encrypted.push_back(votingPublicKey_->encrypt(zero));
    }
    
    // Assign points based on ranking
    for (size_t rank = 0; rank < rankings.size(); rank++) {
        int choiceIndex = rankings[rank];
        int choicePoints = points - static_cast<int>(rank);
        std::vector<uint8_t> pointsBytes = intToBytes(choicePoints);
        vote.encrypted[choiceIndex] = votingPublicKey_->encrypt(pointsBytes);
    }
    
    return vote;
}

EncryptedVote VoteEncoder::encodeRankedChoice(const std::vector<int>& rankings, int choiceCount) const {
    EncryptedVote vote;
    vote.rankings = rankings;
    
    std::vector<uint8_t> zero = intToBytes(0);
    
    // Initialize all to 0 (not ranked)
    for (int i = 0; i < choiceCount; i++) {
        vote.encrypted.push_back(votingPublicKey_->encrypt(zero));
    }
    
    // Store rank position (1-indexed, 0 means not ranked)
    for (size_t rank = 0; rank < rankings.size(); rank++) {
        int choiceIndex = rankings[rank];
        std::vector<uint8_t> rankBytes = intToBytes(static_cast<int>(rank + 1));
        vote.encrypted[choiceIndex] = votingPublicKey_->encrypt(rankBytes);
    }
    
    return vote;
}

EncryptedVote VoteEncoder::encode(
    VotingMethod method,
    const std::optional<int>& choiceIndex,
    const std::optional<std::vector<int>>& choices,
    const std::optional<std::vector<int>>& rankings,
    const std::optional<std::vector<uint8_t>>& weight,
    int choiceCount
) const {
    switch (method) {
        case VotingMethod::Plurality:
            if (!choiceIndex.has_value()) {
                throw std::invalid_argument("Choice required");
            }
            return encodePlurality(choiceIndex.value(), choiceCount);

        case VotingMethod::Approval:
            if (!choices.has_value()) {
                throw std::invalid_argument("Choices required");
            }
            return encodeApproval(choices.value(), choiceCount);

        case VotingMethod::Weighted:
            if (!choiceIndex.has_value() || !weight.has_value()) {
                throw std::invalid_argument("Choice and weight required");
            }
            return encodeWeighted(choiceIndex.value(), weight.value(), choiceCount);

        case VotingMethod::Borda:
            if (!rankings.has_value()) {
                throw std::invalid_argument("Rankings required");
            }
            return encodeBorda(rankings.value(), choiceCount);

        case VotingMethod::RankedChoice:
            if (!rankings.has_value()) {
                throw std::invalid_argument("Rankings required");
            }
            return encodeRankedChoice(rankings.value(), choiceCount);

        case VotingMethod::Quadratic:
            if (!choiceIndex.has_value() || !weight.has_value()) {
                throw std::invalid_argument("Choice and weight required");
            }
            return encodeWeighted(choiceIndex.value(), weight.value(), choiceCount);

        case VotingMethod::Consensus:
            if (!choiceIndex.has_value()) {
                throw std::invalid_argument("Choice required");
            }
            return encodePlurality(choiceIndex.value(), choiceCount);

        case VotingMethod::ConsentBased: {
            if (!choiceIndex.has_value()) {
                throw std::invalid_argument("Choice required");
            }
            // Encode: 1 = support, 0 = neutral, -1 = strong objection
            std::vector<uint8_t> voteWeight = weight.has_value() ? weight.value() : intToBytes(1);
            return encodeWeighted(choiceIndex.value(), voteWeight, choiceCount);
        }

        default:
            throw std::invalid_argument("Unknown voting method");
    }
}

} // namespace brightchain
