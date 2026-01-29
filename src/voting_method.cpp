#include <brightchain/voting_method.hpp>
#include <stdexcept>
#include <map>

namespace brightchain {

static const std::map<VotingMethod, std::string> METHOD_TO_STRING = {
    {VotingMethod::Plurality, "plurality"},
    {VotingMethod::Approval, "approval"},
    {VotingMethod::Weighted, "weighted"},
    {VotingMethod::Borda, "borda"},
    {VotingMethod::Score, "score"},
    {VotingMethod::YesNo, "yes-no"},
    {VotingMethod::YesNoAbstain, "yes-no-abstain"},
    {VotingMethod::Supermajority, "supermajority"},
    {VotingMethod::RankedChoice, "ranked-choice"},
    {VotingMethod::TwoRound, "two-round"},
    {VotingMethod::STAR, "star"},
    {VotingMethod::STV, "stv"},
    {VotingMethod::Quadratic, "quadratic"},
    {VotingMethod::Consensus, "consensus"},
    {VotingMethod::ConsentBased, "consent-based"}
};

static const std::map<VotingMethod, SecurityLevel> METHOD_SECURITY = {
    {VotingMethod::Plurality, SecurityLevel::FullyHomomorphic},
    {VotingMethod::Approval, SecurityLevel::FullyHomomorphic},
    {VotingMethod::Weighted, SecurityLevel::FullyHomomorphic},
    {VotingMethod::Borda, SecurityLevel::FullyHomomorphic},
    {VotingMethod::Score, SecurityLevel::FullyHomomorphic},
    {VotingMethod::YesNo, SecurityLevel::FullyHomomorphic},
    {VotingMethod::YesNoAbstain, SecurityLevel::FullyHomomorphic},
    {VotingMethod::Supermajority, SecurityLevel::FullyHomomorphic},
    {VotingMethod::RankedChoice, SecurityLevel::MultiRound},
    {VotingMethod::TwoRound, SecurityLevel::MultiRound},
    {VotingMethod::STAR, SecurityLevel::MultiRound},
    {VotingMethod::STV, SecurityLevel::MultiRound},
    {VotingMethod::Quadratic, SecurityLevel::Insecure},
    {VotingMethod::Consensus, SecurityLevel::Insecure},
    {VotingMethod::ConsentBased, SecurityLevel::Insecure}
};

std::string votingMethodToString(VotingMethod method) {
    auto it = METHOD_TO_STRING.find(method);
    if (it == METHOD_TO_STRING.end()) {
        throw std::invalid_argument("Unknown voting method");
    }
    return it->second;
}

VotingMethod stringToVotingMethod(const std::string& str) {
    for (const auto& [method, name] : METHOD_TO_STRING) {
        if (name == str) {
            return method;
        }
    }
    throw std::invalid_argument("Unknown voting method string: " + str);
}

SecurityLevel getSecurityLevel(VotingMethod method) {
    auto it = METHOD_SECURITY.find(method);
    if (it == METHOD_SECURITY.end()) {
        throw std::invalid_argument("Unknown voting method");
    }
    return it->second;
}

} // namespace brightchain
