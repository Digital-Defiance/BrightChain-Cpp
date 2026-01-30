#include <brightchain/member.hpp>
#include <brightchain/poll_factory.hpp>
#include <brightchain/vote_encoder.hpp>
#include <brightchain/poll_tallier.hpp>
#include <iostream>

using namespace brightchain;

void examplePlurality() {
    std::cout << "\n=== Plurality Voting ===\n";
    
    auto authority = Member::generate(MemberType::System, "Authority", "auth@test.com");
    authority.deriveVotingKeys(512, 16);
    
    auto poll = PollFactory::createPlurality({"Alice", "Bob", "Charlie"}, authority);
    VoteEncoder encoder(poll->votingPublicKey());
    
    auto voter1 = Member::generate(MemberType::User, "V1", "v1@test.com");
    auto voter2 = Member::generate(MemberType::User, "V2", "v2@test.com");
    auto voter3 = Member::generate(MemberType::User, "V3", "v3@test.com");
    
    poll->vote(voter1, encoder.encodePlurality(0, 3));
    poll->vote(voter2, encoder.encodePlurality(0, 3));
    poll->vote(voter3, encoder.encodePlurality(1, 3));
    poll->close();
    
    PollTallier tallier(authority, authority.votingPrivateKey(), authority.votingPublicKey());
    auto results = tallier.tally(*poll);
    
    std::cout << "Winner: " << results.choices[results.winner.value()] << "\n";
}

void exampleApproval() {
    std::cout << "\n=== Approval Voting ===\n";
    
    auto authority = Member::generate(MemberType::System, "Authority", "auth@test.com");
    authority.deriveVotingKeys(512, 16);
    
    auto poll = PollFactory::createApproval({"Red", "Green", "Blue", "Yellow"}, authority);
    VoteEncoder encoder(poll->votingPublicKey());
    
    auto voter1 = Member::generate(MemberType::User, "V1", "v1@test.com");
    auto voter2 = Member::generate(MemberType::User, "V2", "v2@test.com");
    
    poll->vote(voter1, encoder.encodeApproval({0, 2}, 4));
    poll->vote(voter2, encoder.encodeApproval({1, 2}, 4));
    poll->close();
    
    PollTallier tallier(authority, authority.votingPrivateKey(), authority.votingPublicKey());
    auto results = tallier.tally(*poll);
    
    std::cout << "Winner: " << results.choices[results.winner.value()] << "\n";
}

void exampleRankedChoice() {
    std::cout << "\n=== Ranked Choice Voting ===\n";
    
    auto authority = Member::generate(MemberType::System, "Authority", "auth@test.com");
    authority.deriveVotingKeys(512, 16);
    
    auto poll = PollFactory::createRankedChoice({"Alice", "Bob", "Charlie"}, authority);
    VoteEncoder encoder(poll->votingPublicKey());
    
    auto voter1 = Member::generate(MemberType::User, "V1", "v1@test.com");
    auto voter2 = Member::generate(MemberType::User, "V2", "v2@test.com");
    
    poll->vote(voter1, encoder.encodeRankedChoice({0, 1, 2}, 3));
    poll->vote(voter2, encoder.encodeRankedChoice({1, 0, 2}, 3));
    poll->close();
    
    PollTallier tallier(authority, authority.votingPrivateKey(), authority.votingPublicKey());
    auto results = tallier.tally(*poll);
    
    std::cout << "Winner: " << results.choices[results.winner.value()] << "\n";
}

int main() {
    std::cout << "BrightChain Voting System Examples\n";
    std::cout << "===================================\n";
    
    try {
        examplePlurality();
        exampleApproval();
        exampleRankedChoice();
        
        std::cout << "\n✓ All examples completed!\n";
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
