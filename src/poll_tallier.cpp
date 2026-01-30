#include <brightchain/poll_tallier.hpp>
#include <stdexcept>
#include <algorithm>
#include <set>

namespace brightchain {

static int64_t bytesToInt(const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return 0;
    int64_t result = 0;
    for (size_t i = 0; i < bytes.size() && i < 8; i++) {
        result |= (static_cast<int64_t>(bytes[i]) << (i * 8));
    }
    return result;
}

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
    if (negative) {
        result.push_back(0xFF);
    }
    return result;
}

static std::vector<uint8_t> addBigintBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    int64_t aVal = bytesToInt(a);
    int64_t bVal = bytesToInt(b);
    return intToBytes(aVal + bVal);
}

static bool compareBigintBytes(const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
    int64_t aVal = bytesToInt(a);
    int64_t bVal = bytesToInt(b);
    return aVal < bVal;
}

PollTallier::PollTallier(
    const Member& authority,
    std::shared_ptr<PaillierPrivateKey> votingPrivateKey,
    std::shared_ptr<PaillierPublicKey> votingPublicKey
)
    : authority_(authority)
    , votingPrivateKey_(votingPrivateKey)
    , votingPublicKey_(votingPublicKey)
{
    if (!authority.hasVotingPrivateKey()) {
        throw std::invalid_argument("Authority must have private key");
    }
}

PollResults PollTallier::tally(const Poll& poll) {
    if (!poll.isClosed()) {
        throw std::runtime_error("Poll must be closed");
    }
    auto votes = poll.getEncryptedVotes();
    int choiceCount = static_cast<int>(poll.choices().size());
    switch (poll.method()) {
        case VotingMethod::Plurality:
        case VotingMethod::Approval:
        case VotingMethod::Weighted:
            return tallyAdditive(poll, votes, choiceCount);
        case VotingMethod::Borda:
        case VotingMethod::Score:
            return tallyScored(poll, votes, choiceCount);
        case VotingMethod::YesNo:
        case VotingMethod::YesNoAbstain:
        case VotingMethod::Supermajority:
            return tallyYesNo(poll, votes, choiceCount);
        case VotingMethod::RankedChoice:
            return tallyRankedChoice(poll, votes, choiceCount);
        case VotingMethod::TwoRound:
            return tallyTwoRound(poll, votes, choiceCount);
        case VotingMethod::STAR:
            return tallySTAR(poll, votes, choiceCount);
        case VotingMethod::STV:
            return tallySTV(poll, votes, choiceCount);
        case VotingMethod::Quadratic:
            return tallyQuadratic(poll, votes, choiceCount);
        case VotingMethod::Consensus:
            return tallyConsensus(poll, votes, choiceCount);
        case VotingMethod::ConsentBased:
            return tallyConsentBased(poll, votes, choiceCount);
        default:
            throw std::runtime_error("Unknown voting method");
    }
}

PollResults PollTallier::tallyAdditive(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    std::vector<std::vector<uint8_t>> tallies(choiceCount, intToBytes(0));
    for (const auto& [_, encrypted] : votes) {
        for (int i = 0; i < choiceCount; i++) {
            std::vector<uint8_t> decrypted = votingPrivateKey_->decrypt(encrypted[i]);
            tallies[i] = addBigintBytes(tallies[i], decrypted);
        }
    }
    std::vector<uint8_t> maxVotes = tallies[0];
    for (const auto& tally : tallies) {
        if (compareBigintBytes(maxVotes, tally)) {
            maxVotes = tally;
        }
    }
    std::vector<int> winners;
    for (int i = 0; i < choiceCount; i++) {
        if (!compareBigintBytes(tallies[i], maxVotes) && !compareBigintBytes(maxVotes, tallies[i])) {
            winners.push_back(i);
        }
    }
    PollResults results;
    results.method = poll.method();
    results.choices = poll.choices();
    results.tallies = tallies;
    results.voterCount = poll.voterCount();
    if (winners.size() == 1) {
        results.winner = winners[0];
    } else if (winners.size() > 1) {
        results.winners = winners;
    }
    return results;
}

PollResults PollTallier::tallyScored(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    return tallyAdditive(poll, votes, choiceCount);
}

PollResults PollTallier::tallyYesNo(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    return tallyAdditive(poll, votes, choiceCount);
}

std::vector<std::vector<int>> PollTallier::decryptRankings(
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    std::vector<std::vector<int>> rankings;
    for (const auto& [_, encrypted] : votes) {
        std::vector<std::pair<int, int>> rankedChoices;
        for (int i = 0; i < choiceCount; i++) {
            std::vector<uint8_t> decrypted = votingPrivateKey_->decrypt(encrypted[i]);
            int rank = static_cast<int>(bytesToInt(decrypted));
            if (rank > 0) {
                rankedChoices.push_back({i, rank});
            }
        }
        std::sort(rankedChoices.begin(), rankedChoices.end(),
                  [](const auto& a, const auto& b) { return a.second < b.second; });
        std::vector<int> ranking;
        for (const auto& [choice, _] : rankedChoices) {
            ranking.push_back(choice);
        }
        rankings.push_back(ranking);
    }
    return rankings;
}

std::vector<std::vector<uint8_t>> PollTallier::countFirstChoices(
    const std::vector<std::vector<int>>& rankings,
    const std::set<int>& eliminated,
    int choiceCount
) {
    std::vector<std::vector<uint8_t>> tallies(choiceCount, intToBytes(0));
    for (const auto& ranking : rankings) {
        for (int choice : ranking) {
            if (eliminated.find(choice) == eliminated.end()) {
                tallies[choice] = addBigintBytes(tallies[choice], intToBytes(1));
                break;
            }
        }
    }
    return tallies;
}

PollResults PollTallier::tallyRankedChoice(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    std::vector<RoundResult> rounds;
    std::set<int> eliminated;
    auto rankings = decryptRankings(votes, choiceCount);
    int round = 0;
    while (true) {
        round++;
        auto tallies = countFirstChoices(rankings, eliminated, choiceCount);
        std::vector<uint8_t> totalVotes = intToBytes(0);
        for (const auto& tally : tallies) {
            totalVotes = addBigintBytes(totalVotes, tally);
        }
        int64_t totalInt = bytesToInt(totalVotes);
        int64_t majority = totalInt / 2;
        std::vector<uint8_t> maxVotes = intToBytes(0);
        for (int i = 0; i < choiceCount; i++) {
            if (eliminated.find(i) == eliminated.end() && compareBigintBytes(maxVotes, tallies[i])) {
                maxVotes = tallies[i];
            }
        }
        std::vector<int> topCandidates;
        for (int i = 0; i < choiceCount; i++) {
            if (eliminated.find(i) == eliminated.end() &&
                !compareBigintBytes(tallies[i], maxVotes) &&
                !compareBigintBytes(maxVotes, tallies[i])) {
                topCandidates.push_back(i);
            }
        }
        RoundResult roundResult;
        roundResult.round = round;
        roundResult.tallies = tallies;
        rounds.push_back(roundResult);
        int64_t maxInt = bytesToInt(maxVotes);
        if (maxInt > majority && topCandidates.size() == 1) {
            rounds.back().winner = topCandidates[0];
            PollResults results;
            results.method = VotingMethod::RankedChoice;
            results.choices = poll.choices();
            results.winner = topCandidates[0];
            results.eliminated = std::vector<int>(eliminated.begin(), eliminated.end());
            results.rounds = rounds;
            results.tallies = tallies;
            results.voterCount = poll.voterCount();
            return results;
        }
        int remaining = choiceCount - static_cast<int>(eliminated.size());
        if (remaining == 1) {
            int winner = -1;
            for (int i = 0; i < choiceCount; i++) {
                if (eliminated.find(i) == eliminated.end()) {
                    winner = i;
                    break;
                }
            }
            rounds.back().winner = winner;
            PollResults results;
            results.method = VotingMethod::RankedChoice;
            results.choices = poll.choices();
            results.winner = winner;
            results.eliminated = std::vector<int>(eliminated.begin(), eliminated.end());
            results.rounds = rounds;
            results.tallies = tallies;
            results.voterCount = poll.voterCount();
            return results;
        }
        std::vector<uint8_t> minVotes = totalVotes;
        int toEliminate = -1;
        for (int i = choiceCount - 1; i >= 0; i--) {
            if (eliminated.find(i) == eliminated.end() &&
                (compareBigintBytes(tallies[i], minVotes) ||
                 (!compareBigintBytes(tallies[i], minVotes) && !compareBigintBytes(minVotes, tallies[i])))) {
                minVotes = tallies[i];
                toEliminate = i;
            }
        }
        if (toEliminate == -1) break;
        eliminated.insert(toEliminate);
        rounds.back().eliminated = toEliminate;
    }
    auto finalTallies = countFirstChoices(rankings, eliminated, choiceCount);
    PollResults results;
    results.method = VotingMethod::RankedChoice;
    results.choices = poll.choices();
    results.winner = 0;
    results.eliminated = std::vector<int>(eliminated.begin(), eliminated.end());
    results.rounds = rounds;
    results.tallies = finalTallies;
    results.voterCount = poll.voterCount();
    return results;
}

PollResults PollTallier::tallyQuadratic(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    std::vector<std::vector<uint8_t>> tallies(choiceCount, intToBytes(0));
    for (const auto& [_, encrypted] : votes) {
        for (int i = 0; i < choiceCount; i++) {
            std::vector<uint8_t> decrypted = votingPrivateKey_->decrypt(encrypted[i]);
            int64_t weight = bytesToInt(decrypted);
            int64_t quadratic = weight * weight;
            tallies[i] = addBigintBytes(tallies[i], intToBytes(quadratic));
        }
    }
    std::vector<uint8_t> maxVotes = tallies[0];
    for (const auto& tally : tallies) {
        if (compareBigintBytes(maxVotes, tally)) {
            maxVotes = tally;
        }
    }
    std::vector<int> winners;
    for (int i = 0; i < choiceCount; i++) {
        if (!compareBigintBytes(tallies[i], maxVotes) && !compareBigintBytes(maxVotes, tallies[i])) {
            winners.push_back(i);
        }
    }
    PollResults results;
    results.method = VotingMethod::Quadratic;
    results.choices = poll.choices();
    results.tallies = tallies;
    results.voterCount = poll.voterCount();
    if (winners.size() == 1) {
        results.winner = winners[0];
    } else if (winners.size() > 1) {
        results.winners = winners;
    }
    return results;
}

PollResults PollTallier::tallyConsensus(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    std::vector<std::vector<uint8_t>> tallies(choiceCount, intToBytes(0));
    int64_t totalVoters = static_cast<int64_t>(votes.size());
    for (const auto& [_, encrypted] : votes) {
        for (int i = 0; i < choiceCount; i++) {
            std::vector<uint8_t> decrypted = votingPrivateKey_->decrypt(encrypted[i]);
            tallies[i] = addBigintBytes(tallies[i], decrypted);
        }
    }
    // Consensus requires > 95% (not >=)
    // Use ceiling division: threshold = ceil(totalVoters * 0.95)
    int64_t threshold = ((totalVoters * 95) + 99) / 100;
    std::vector<int> winners;
    for (int i = 0; i < choiceCount; i++) {
        int64_t voteCount = bytesToInt(tallies[i]);
        if (voteCount >= threshold) {
            winners.push_back(i);
        }
    }
    PollResults results;
    results.method = VotingMethod::Consensus;
    results.choices = poll.choices();
    results.tallies = tallies;
    results.voterCount = poll.voterCount();
    if (winners.size() == 1) {
        results.winner = winners[0];
    } else if (!winners.empty()) {
        results.winners = winners;
    }
    return results;
}

PollResults PollTallier::tallyConsentBased(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    std::vector<std::vector<uint8_t>> tallies(choiceCount, intToBytes(0));
    std::vector<std::vector<uint8_t>> objections(choiceCount, intToBytes(0));
    for (const auto& [_, encrypted] : votes) {
        for (int i = 0; i < choiceCount; i++) {
            std::vector<uint8_t> decrypted = votingPrivateKey_->decrypt(encrypted[i]);
            int64_t vote = bytesToInt(decrypted);
            if (vote > 0) {
                tallies[i] = addBigintBytes(tallies[i], intToBytes(1));
            } else if (vote < 0) {
                objections[i] = addBigintBytes(objections[i], intToBytes(1));
            }
        }
    }
    std::vector<int> winners;
    for (int i = 0; i < choiceCount; i++) {
        if (bytesToInt(objections[i]) == 0) {
            winners.push_back(i);
        }
    }
    PollResults results;
    results.method = VotingMethod::ConsentBased;
    results.choices = poll.choices();
    results.tallies = tallies;
    results.voterCount = poll.voterCount();
    if (winners.size() == 1) {
        results.winner = winners[0];
    } else if (!winners.empty()) {
        results.winners = winners;
    }
    return results;
}

PollResults PollTallier::tallyTwoRound(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    std::vector<RoundResult> rounds;
    std::vector<std::vector<uint8_t>> tallies(choiceCount, intToBytes(0));
    for (const auto& [_, encrypted] : votes) {
        for (int i = 0; i < choiceCount; i++) {
            std::vector<uint8_t> decrypted = votingPrivateKey_->decrypt(encrypted[i]);
            tallies[i] = addBigintBytes(tallies[i], decrypted);
        }
    }
    std::vector<uint8_t> totalVotes = intToBytes(0);
    for (const auto& tally : tallies) {
        totalVotes = addBigintBytes(totalVotes, tally);
    }
    int64_t totalInt = bytesToInt(totalVotes);
    int64_t majority = totalInt / 2;
    RoundResult round1;
    round1.round = 1;
    round1.tallies = tallies;
    rounds.push_back(round1);
    std::vector<uint8_t> maxVotes = tallies[0];
    for (const auto& tally : tallies) {
        if (compareBigintBytes(maxVotes, tally)) {
            maxVotes = tally;
        }
    }
    int64_t maxInt = bytesToInt(maxVotes);
    if (maxInt > majority) {
        int winner = -1;
        for (int i = 0; i < choiceCount; i++) {
            if (!compareBigintBytes(tallies[i], maxVotes) && !compareBigintBytes(maxVotes, tallies[i])) {
                winner = i;
                break;
            }
        }
        rounds[0].winner = winner;
        PollResults results;
        results.method = VotingMethod::TwoRound;
        results.choices = poll.choices();
        results.winner = winner;
        results.rounds = rounds;
        results.tallies = tallies;
        results.voterCount = poll.voterCount();
        return results;
    }
    std::vector<std::pair<int, std::vector<uint8_t>>> sorted;
    for (int i = 0; i < choiceCount; i++) {
        sorted.push_back({i, tallies[i]});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return compareBigintBytes(b.second, a.second); });
    int winner = sorted[0].first;
    std::vector<std::vector<uint8_t>> runoffTallies(choiceCount, intToBytes(0));
    runoffTallies[sorted[0].first] = sorted[0].second;
    runoffTallies[sorted[1].first] = sorted[1].second;
    RoundResult round2;
    round2.round = 2;
    round2.tallies = runoffTallies;
    round2.winner = winner;
    rounds.push_back(round2);
    PollResults results;
    results.method = VotingMethod::TwoRound;
    results.choices = poll.choices();
    results.winner = winner;
    results.rounds = rounds;
    results.tallies = runoffTallies;
    results.voterCount = poll.voterCount();
    return results;
}

PollResults PollTallier::tallySTAR(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    std::vector<RoundResult> rounds;
    std::vector<std::vector<uint8_t>> scores(choiceCount, intToBytes(0));
    for (const auto& [_, encrypted] : votes) {
        for (int i = 0; i < choiceCount; i++) {
            std::vector<uint8_t> decrypted = votingPrivateKey_->decrypt(encrypted[i]);
            scores[i] = addBigintBytes(scores[i], decrypted);
        }
    }
    RoundResult round1;
    round1.round = 1;
    round1.tallies = scores;
    rounds.push_back(round1);
    std::vector<std::pair<int, std::vector<uint8_t>>> sorted;
    for (int i = 0; i < choiceCount; i++) {
        sorted.push_back({i, scores[i]});
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return compareBigintBytes(b.second, a.second); });
    int top0 = sorted[0].first;
    int top1 = sorted[1].first;
    std::vector<std::vector<uint8_t>> runoffTallies(choiceCount, intToBytes(0));
    for (const auto& [_, encrypted] : votes) {
        std::vector<uint8_t> score0 = votingPrivateKey_->decrypt(encrypted[top0]);
        std::vector<uint8_t> score1 = votingPrivateKey_->decrypt(encrypted[top1]);
        if (compareBigintBytes(score1, score0)) {
            runoffTallies[top0] = addBigintBytes(runoffTallies[top0], intToBytes(1));
        } else if (compareBigintBytes(score0, score1)) {
            runoffTallies[top1] = addBigintBytes(runoffTallies[top1], intToBytes(1));
        }
    }
    int winner = compareBigintBytes(runoffTallies[top1], runoffTallies[top0]) ? top0 : top1;
    RoundResult round2;
    round2.round = 2;
    round2.tallies = runoffTallies;
    round2.winner = winner;
    rounds.push_back(round2);
    PollResults results;
    results.method = VotingMethod::STAR;
    results.choices = poll.choices();
    results.winner = winner;
    results.rounds = rounds;
    results.tallies = runoffTallies;
    results.voterCount = poll.voterCount();
    return results;
}

PollResults PollTallier::tallySTV(
    const Poll& poll,
    const std::map<std::string, std::vector<std::vector<uint8_t>>>& votes,
    int choiceCount
) {
    std::vector<RoundResult> rounds;
    std::set<int> eliminated;
    std::vector<int> winners;
    auto rankings = decryptRankings(votes, choiceCount);
    int seatsToFill = std::min(3, choiceCount);
    int64_t quota = static_cast<int64_t>(votes.size()) / (seatsToFill + 1) + 1;
    int round = 0;
    while (static_cast<int>(winners.size()) < seatsToFill && static_cast<int>(eliminated.size()) < choiceCount) {
        round++;
        auto tallies = countFirstChoices(rankings, eliminated, choiceCount);
        RoundResult roundResult;
        roundResult.round = round;
        roundResult.tallies = tallies;
        rounds.push_back(roundResult);
        std::vector<int> meetingQuota;
        for (int i = 0; i < choiceCount; i++) {
            if (eliminated.find(i) == eliminated.end() &&
                std::find(winners.begin(), winners.end(), i) == winners.end() &&
                bytesToInt(tallies[i]) >= quota) {
                meetingQuota.push_back(i);
            }
        }
        if (!meetingQuota.empty()) {
            winners.insert(winners.end(), meetingQuota.begin(), meetingQuota.end());
            for (int i : meetingQuota) {
                eliminated.insert(i);
            }
            rounds.back().winner = meetingQuota[0];
            continue;
        }
        std::vector<std::pair<int, std::vector<uint8_t>>> remaining;
        for (int i = 0; i < choiceCount; i++) {
            if (eliminated.find(i) == eliminated.end() &&
                std::find(winners.begin(), winners.end(), i) == winners.end()) {
                remaining.push_back({i, tallies[i]});
            }
        }
        if (remaining.empty()) break;
        std::sort(remaining.begin(), remaining.end(),
                  [](const auto& a, const auto& b) { return compareBigintBytes(a.second, b.second); });
        int toEliminate = remaining[0].first;
        eliminated.insert(toEliminate);
        rounds.back().eliminated = toEliminate;
    }
    std::vector<std::vector<uint8_t>> finalTallies(choiceCount, intToBytes(0));
    for (int w : winners) {
        finalTallies[w] = intToBytes(1);
    }
    PollResults results;
    results.method = VotingMethod::STV;
    results.choices = poll.choices();
    results.winners = winners;
    results.eliminated = std::vector<int>(eliminated.begin(), eliminated.end());
    results.rounds = rounds;
    results.tallies = finalTallies;
    results.voterCount = poll.voterCount();
    return results;
}

} // namespace brightchain
