#include <brightchain/poll_factory.hpp>
#include <random>

namespace brightchain {

static std::vector<uint8_t> generatePollId() {
    std::vector<uint8_t> id(16);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (auto& byte : id) {
        byte = static_cast<uint8_t>(dis(gen));
    }
    return id;
}

std::unique_ptr<Poll> PollFactory::create(
    const std::vector<std::string>& choices,
    VotingMethod method,
    const Member& authority,
    const std::optional<std::vector<uint8_t>>& maxWeight
) {
    if (!authority.hasVotingKeys()) {
        throw std::invalid_argument("Authority must have voting public key");
    }
    
    auto id = generatePollId();
    return std::make_unique<Poll>(id, choices, method, authority, authority.votingPublicKey(), maxWeight);
}

std::unique_ptr<Poll> PollFactory::createPlurality(const std::vector<std::string>& choices, const Member& authority) {
    return create(choices, VotingMethod::Plurality, authority);
}

std::unique_ptr<Poll> PollFactory::createApproval(const std::vector<std::string>& choices, const Member& authority) {
    return create(choices, VotingMethod::Approval, authority);
}

std::unique_ptr<Poll> PollFactory::createWeighted(const std::vector<std::string>& choices, const Member& authority, const std::vector<uint8_t>& maxWeight) {
    return create(choices, VotingMethod::Weighted, authority, maxWeight);
}

std::unique_ptr<Poll> PollFactory::createBorda(const std::vector<std::string>& choices, const Member& authority) {
    return create(choices, VotingMethod::Borda, authority);
}

std::unique_ptr<Poll> PollFactory::createRankedChoice(const std::vector<std::string>& choices, const Member& authority) {
    return create(choices, VotingMethod::RankedChoice, authority);
}

std::unique_ptr<Poll> PollFactory::createSTAR(const std::vector<std::string>& choices, const Member& authority, int maxScore) {
    return create(choices, VotingMethod::STAR, authority);
}

std::unique_ptr<Poll> PollFactory::createQuadratic(const std::vector<std::string>& choices, const Member& authority, const std::vector<uint8_t>& maxCredits) {
    return create(choices, VotingMethod::Quadratic, authority, maxCredits);
}

} // namespace brightchain
