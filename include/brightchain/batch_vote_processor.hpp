#pragma once

#include <vector>
#include <functional>
#include <cstddef>

namespace brightchain {

/**
 * Vote batch structure
 */
template<typename TVoter, typename TVote>
struct VoteBatch {
    TVoter voter;
    TVote vote;
};

/**
 * Memory-efficient batch processor for large-scale elections
 */
template<typename TVoter, typename TVote>
class BatchVoteProcessor {
public:
    using Batch = std::vector<VoteBatch<TVoter, TVote>>;
    using ProcessorFunc = std::function<void(const Batch&)>;
    
    explicit BatchVoteProcessor(size_t batchSize = 1000)
        : batchSize_(batchSize) {}
    
    /**
     * Add vote to current batch
     * @return true if batch is full and should be processed
     */
    bool addVote(const TVoter& voter, const TVote& vote) {
        currentBatch_.push_back({voter, vote});
        return currentBatch_.size() >= batchSize_;
    }
    
    /**
     * Process current batch and clear it
     */
    void processBatch(ProcessorFunc processor) {
        if (currentBatch_.empty()) return;
        processor(currentBatch_);
        currentBatch_.clear();
    }
    
    /**
     * Get current batch size
     */
    size_t getBatchSize() const {
        return currentBatch_.size();
    }
    
    /**
     * Get configured batch size limit
     */
    size_t getBatchSizeLimit() const {
        return batchSize_;
    }

private:
    size_t batchSize_;
    Batch currentBatch_;
};

} // namespace brightchain
