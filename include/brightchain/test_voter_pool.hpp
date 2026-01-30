#pragma once

#include "member.hpp"
#include <vector>
#include <memory>
#include <stdexcept>

namespace brightchain {

/**
 * Test voter pool for voting system testing
 * Manages pre-initialized voters and authority for performance testing
 */
class TestVoterPool {
public:
    /**
     * Initialize pool with specified number of voters
     */
    static void initialize(size_t poolSize = 1000) {
        if (initialized_) return;
        
        // Create authority
        authority_ = std::make_unique<Member>(
            Member::generate(MemberType::System, "Authority", "auth@test.com")
        );
        authority_->deriveVotingKeys(2048, 64);
        
        // Create voters
        voters_.reserve(poolSize);
        for (size_t i = 0; i < poolSize; i++) {
            auto voter = Member::generate(
                MemberType::User,
                "Voter" + std::to_string(i),
                "voter" + std::to_string(i) + "@test.com"
            );
            voter.deriveVotingKeys(2048, 64);
            voters_.push_back(std::move(voter));
        }
        
        initialized_ = true;
    }
    
    /**
     * Get authority member
     */
    static const Member& getAuthority() {
        if (!authority_) {
            throw std::runtime_error("Pool not initialized");
        }
        return *authority_;
    }
    
    /**
     * Get voter by index
     */
    static const Member& getVoter(size_t index) {
        if (!initialized_) {
            throw std::runtime_error("Pool not initialized");
        }
        if (index >= voters_.size()) {
            throw std::out_of_range(
                "Voter index " + std::to_string(index) + 
                " out of range [0, " + std::to_string(voters_.size()) + ")"
            );
        }
        return voters_[index];
    }
    
    /**
     * Get multiple voters
     */
    static std::vector<Member> getVoters(size_t count, size_t startIndex = 0) {
        if (!initialized_) {
            throw std::runtime_error("Pool not initialized");
        }
        if (startIndex + count > voters_.size()) {
            throw std::out_of_range(
                "Not enough voters: requested " + std::to_string(count) +
                " from " + std::to_string(startIndex) +
                ", pool size " + std::to_string(voters_.size())
            );
        }
        
        std::vector<Member> result;
        result.reserve(count);
        for (size_t i = startIndex; i < startIndex + count; i++) {
            result.push_back(voters_[i]);
        }
        return result;
    }
    
    /**
     * Get pool size
     */
    static size_t getPoolSize() {
        return voters_.size();
    }
    
    /**
     * Reset pool
     */
    static void reset() {
        voters_.clear();
        authority_.reset();
        initialized_ = false;
    }
    
    /**
     * Check if initialized
     */
    static bool isInitialized() {
        return initialized_;
    }

private:
    static std::vector<Member> voters_;
    static std::unique_ptr<Member> authority_;
    static bool initialized_;
};

// Static member initialization
inline std::vector<Member> TestVoterPool::voters_;
inline std::unique_ptr<Member> TestVoterPool::authority_;
inline bool TestVoterPool::initialized_ = false;

} // namespace brightchain
