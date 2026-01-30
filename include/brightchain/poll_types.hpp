#pragma once

#include <brightchain/member.hpp>
#include <vector>
#include <cstdint>
#include <ctime>

namespace brightchain {

/**
 * Cryptographically signed receipt proving a vote was cast.
 * Can be used to verify participation without revealing vote content.
 */
struct VoteReceipt {
    std::vector<uint8_t> voterId;      // Member ID bytes
    std::vector<uint8_t> pollId;       // Poll ID bytes
    int64_t timestamp;                  // Unix timestamp when vote was cast
    std::vector<uint8_t> signature;     // Cryptographic signature from poll authority
    std::vector<uint8_t> nonce;         // Random nonce for uniqueness
};

/**
 * Results from a single round of multi-round voting.
 * Used in RCV, Two-Round, STAR, and STV methods.
 */
struct RoundResult {
    int round;                          // Round number (1-indexed)
    std::vector<std::vector<uint8_t>> tallies;  // Vote tallies for this round (bigints as bytes)
    std::optional<int> eliminated;      // Index of choice eliminated this round (if any)
    std::optional<int> winner;          // Index of winner determined this round (if any)
};

/**
 * Results of a completed poll after tallying.
 * Includes winner(s), tallies, and round-by-round data for multi-round methods.
 */
struct PollResults {
    VotingMethod method;                // Voting method used
    std::vector<std::string> choices;   // Array of choice names
    std::optional<int> winner;          // Index of winning choice (undefined if tie)
    std::optional<std::vector<int>> winners;  // Indices of tied winners (for ties or multi-winner methods)
    std::optional<std::vector<int>> eliminated;  // Indices of eliminated choices (for RCV)
    std::optional<std::vector<RoundResult>> rounds;  // Round-by-round results (for multi-round methods)
    std::vector<std::vector<uint8_t>> tallies;  // Final vote tallies for each choice (bigints as bytes)
    int voterCount;                     // Total number of unique voters
};

} // namespace brightchain
