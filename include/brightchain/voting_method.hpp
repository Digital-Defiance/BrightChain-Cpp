#pragma once

#include <string>

namespace brightchain {

/**
 * Voting methods supported by the poll system.
 * 
 * Methods are classified by security level:
 * - Fully Homomorphic: Single-round, privacy-preserving (no intermediate decryption)
 * - Multi-Round: Requires decryption between rounds (less secure)
 * - Insecure: Cannot be made secure with Paillier encryption
 */
enum class VotingMethod {
    // ✅ Fully homomorphic (single-round, privacy-preserving)
    Plurality,        // First-past-the-post voting - most votes wins
    Approval,         // Approval voting - vote for multiple candidates
    Weighted,         // Weighted voting - stakeholder voting with configurable weights
    Borda,            // Borda count - ranked voting with point allocation
    Score,            // Score voting - rate candidates 0-10
    YesNo,            // Yes/No referendum
    YesNoAbstain,     // Yes/No/Abstain referendum with abstention option
    Supermajority,    // Supermajority - requires 2/3 or 3/4 threshold

    // ⚠️ Multi-round (requires decryption between rounds)
    RankedChoice,     // Ranked choice voting (IRV) - instant runoff with elimination
    TwoRound,         // Two-round voting - top 2 runoff election
    STAR,             // STAR voting - Score Then Automatic Runoff
    STV,              // Single Transferable Vote - proportional representation

    // ❌ Insecure (requires non-additive operations or reveals individual votes)
    Quadratic,        // Quadratic voting - requires sqrt operation (not homomorphic)
    Consensus,        // Consensus voting - requires 95%+ agreement (no privacy)
    ConsentBased      // Consent-based voting - sociocracy style (no privacy)
};

enum class SecurityLevel {
    FullyHomomorphic,
    MultiRound,
    Insecure
};

// Convert voting method to string
std::string votingMethodToString(VotingMethod method);

// Convert string to voting method
VotingMethod stringToVotingMethod(const std::string& str);

// Get security level for voting method
SecurityLevel getSecurityLevel(VotingMethod method);

} // namespace brightchain
