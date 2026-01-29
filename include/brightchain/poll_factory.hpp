#pragma once

#include <brightchain/poll.hpp>
#include <brightchain/voting_method.hpp>
#include <brightchain/member.hpp>
#include <memory>

namespace brightchain {

class PollFactory {
public:
    static std::unique_ptr<Poll> create(
        const std::vector<std::string>& choices,
        VotingMethod method,
        const Member& authority,
        const std::optional<std::vector<uint8_t>>& maxWeight = std::nullopt
    );
    
    static std::unique_ptr<Poll> createPlurality(const std::vector<std::string>& choices, const Member& authority);
    static std::unique_ptr<Poll> createApproval(const std::vector<std::string>& choices, const Member& authority);
    static std::unique_ptr<Poll> createWeighted(const std::vector<std::string>& choices, const Member& authority, const std::vector<uint8_t>& maxWeight);
    static std::unique_ptr<Poll> createBorda(const std::vector<std::string>& choices, const Member& authority);
    static std::unique_ptr<Poll> createRankedChoice(const std::vector<std::string>& choices, const Member& authority);
    static std::unique_ptr<Poll> createSTAR(const std::vector<std::string>& choices, const Member& authority, int maxScore = 5);
    static std::unique_ptr<Poll> createQuadratic(const std::vector<std::string>& choices, const Member& authority, const std::vector<uint8_t>& maxCredits);
};

} // namespace brightchain
