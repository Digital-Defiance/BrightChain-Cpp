#pragma once

#include <brightchain/voting_method.hpp>
#include <brightchain/encrypted_vote.hpp>
#include <brightchain/poll_types.hpp>
#include <brightchain/member.hpp>
#include <brightchain/paillier.hpp>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace brightchain {

/**
 * Poll aggregates encrypted votes using only public key.
 * Cannot decrypt votes - requires separate Tallier with private key.
 */
class Poll {
public:
    Poll(
        const std::vector<uint8_t>& id,
        const std::vector<std::string>& choices,
        VotingMethod method,
        const Member& authority,
        std::shared_ptr<PaillierPublicKey> votingPublicKey,
        const std::optional<std::vector<uint8_t>>& maxWeight = std::nullopt,
        bool allowInsecure = false
    );

    ~Poll() = default;

    // Accessors
    const std::vector<uint8_t>& id() const { return id_; }
    const std::vector<std::string>& choices() const { return choices_; }
    VotingMethod method() const { return method_; }
    bool isClosed() const { return closedAt_.has_value(); }
    int voterCount() const { return static_cast<int>(receipts_.size()); }
    int64_t createdAt() const { return createdAt_; }
    std::optional<int64_t> closedAt() const { return closedAt_; }
    std::shared_ptr<PaillierPublicKey> votingPublicKey() const { return votingPublicKey_; }

    /**
     * Cast a vote - validates and encrypts based on method
     */
    VoteReceipt vote(const Member& voter, const EncryptedVote& vote);

    /**
     * Verify a receipt is valid for this poll
     */
    bool verifyReceipt(const Member& voter, const VoteReceipt& receipt) const;

    /**
     * Close the poll - no more votes accepted
     */
    void close();

    /**
     * Get encrypted votes for tallying (read-only)
     */
    std::map<std::string, std::vector<std::vector<uint8_t>>> getEncryptedVotes() const;

private:
    void validateVote(const EncryptedVote& vote) const;
    VoteReceipt generateReceipt(const Member& voter);
    std::vector<uint8_t> receiptData(const VoteReceipt& receipt) const;
    std::string toKey(const std::vector<uint8_t>& id) const;
    std::vector<uint8_t> hashVoterId(const std::vector<uint8_t>& voterId) const;

    std::vector<uint8_t> id_;
    std::vector<std::string> choices_;
    VotingMethod method_;
    const Member& authority_;  // Store as reference instead of copy
    std::shared_ptr<PaillierPublicKey> votingPublicKey_;
    std::map<std::string, std::vector<std::vector<uint8_t>>> votes_;
    std::map<std::string, VoteReceipt> receipts_;
    int64_t createdAt_;
    std::optional<int64_t> closedAt_;
    std::optional<std::vector<uint8_t>> maxWeight_;
};

} // namespace brightchain
