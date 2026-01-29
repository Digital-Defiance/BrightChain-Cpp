#pragma once

#include <brightchain/paillier.hpp>
#include <brightchain/voting_method.hpp>
#include <brightchain/encrypted_vote.hpp>
#include <memory>
#include <vector>

namespace brightchain {

/**
 * Vote Encoder - Encrypts votes using Paillier homomorphic encryption
 */
class VoteEncoder {
public:
    explicit VoteEncoder(std::shared_ptr<PaillierPublicKey> votingPublicKey);
    ~VoteEncoder() = default;

    /**
     * Encode a plurality vote (single choice)
     */
    EncryptedVote encodePlurality(int choiceIndex, int choiceCount) const;

    /**
     * Encode an approval vote (multiple choices)
     */
    EncryptedVote encodeApproval(const std::vector<int>& choices, int choiceCount) const;

    /**
     * Encode a weighted vote
     */
    EncryptedVote encodeWeighted(int choiceIndex, const std::vector<uint8_t>& weight, int choiceCount) const;

    /**
     * Encode a Borda count vote (ranked with points)
     * First choice gets N points, second gets N-1, etc.
     */
    EncryptedVote encodeBorda(const std::vector<int>& rankings, int choiceCount) const;

    /**
     * Encode a ranked choice vote (for IRV/STV)
     * Stores ranking order, not points
     */
    EncryptedVote encodeRankedChoice(const std::vector<int>& rankings, int choiceCount) const;

    /**
     * Encode vote based on method
     */
    EncryptedVote encode(
        VotingMethod method,
        const std::optional<int>& choiceIndex,
        const std::optional<std::vector<int>>& choices,
        const std::optional<std::vector<int>>& rankings,
        const std::optional<std::vector<uint8_t>>& weight,
        int choiceCount
    ) const;

private:
    std::shared_ptr<PaillierPublicKey> votingPublicKey_;
};

} // namespace brightchain
