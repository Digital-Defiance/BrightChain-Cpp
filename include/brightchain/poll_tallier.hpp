#pragma once

#include <brightchain/poll.hpp>
#include <brightchain/poll_types.hpp>
#include <brightchain/paillier.hpp>
#include <brightchain/member.hpp>
#include <memory>
#include <set>

namespace brightchain {

/**
 * Poll Tallier - Holds private key, decrypts results
 * Separate from Poll to enforce role separation
 */
class PollTallier {
public:
    PollTallier(
        const Member& authority,
        std::shared_ptr<PaillierPrivateKey> votingPrivateKey,
        std::shared_ptr<PaillierPublicKey> votingPublicKey
    );

    ~PollTallier() = default;

    /**
     * Tally votes and determine winner(s)
     * Can only be called after poll is closed
     */
    PollResults tally(const Poll& poll);

private:
    PollResults tallyAdditive(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    PollResults tallyScored(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    PollResults tallyYesNo(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    PollResults tallyRankedChoice(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    PollResults tallyTwoRound(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    PollResults tallySTAR(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    PollResults tallySTV(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    PollResults tallyQuadratic(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    PollResults tallyConsensus(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    PollResults tallyConsentBased(const Poll& poll, const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);

    std::vector<std::vector<int>> decryptRankings(const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes, int choiceCount);
    std::vector<std::vector<uint8_t>> countFirstChoices(const std::vector<std::vector<int>>& rankings, const std::set<int>& eliminated, int choiceCount);

    const Member& authority_;
    std::shared_ptr<PaillierPrivateKey> votingPrivateKey_;
    std::shared_ptr<PaillierPublicKey> votingPublicKey_;
};

} // namespace brightchain
