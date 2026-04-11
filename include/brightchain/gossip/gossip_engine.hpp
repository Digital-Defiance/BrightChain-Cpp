#pragma once

#include <brightchain/gossip/block_announcement.hpp>
#include <brightchain/gossip/gossip_config.hpp>

#include <functional>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

namespace brightchain {
class Member;
class DiskBlockStore;
class Ecies;
} // namespace brightchain

namespace brightchain::db {
class HeadRegistry;
} // namespace brightchain::db

namespace brightchain::gossip {

class PeerManager;

using AnnouncementHandler = std::function<void(const BlockAnnouncement&)>;

class GossipEngine {
public:
    GossipEngine(
        PeerManager& peerManager,
        DiskBlockStore& blockStore,
        db::HeadRegistry& headRegistry,
        const Member& localNode,
        GossipConfig config = {}
    );

    // Block announcements
    void announceBlock(const std::string& blockId, std::optional<std::string> poolId = {});
    void announceRemoval(const std::string& blockId, std::optional<std::string> poolId = {});

    // Message delivery
    void announceMessage(const std::vector<std::string>& blockIds, const MessageDeliveryMetadata& metadata);
    void sendDeliveryAck(const DeliveryAckMetadata& ack);

    // Head registry sync
    void announceHeadUpdate(const std::string& dbName, const std::string& collectionName,
                            const std::string& blockId, std::optional<WriteProof> proof = {});

    // Pool lifecycle
    void announcePoolCreation(const std::string& poolId, const PoolAnnouncementMetadata& meta);
    void announcePoolRemoval(const std::string& poolId);
    void announcePoolDeletion(const std::string& poolId);

    // BrightTrust
    void announceBrightTrustProposal(const QuorumProposalMetadata& meta);
    void announceBrightTrustVote(const QuorumVoteMetadata& meta);

    // CBL index
    void announceCblIndexUpdate(const std::string& poolId, const CblIndexEntry& entry);
    void announceCblIndexDelete(const std::string& poolId, const CblIndexEntry& entry);

    // ACL
    void announceAclUpdate(const std::string& poolId, const std::string& aclBlockId);

    // Incoming announcement processing
    void handleAnnouncement(const BlockAnnouncement& announcement);

    // Event subscriptions
    void onAnnouncement(AnnouncementHandler handler);
    void onMessageDelivery(AnnouncementHandler handler);
    void onDeliveryAck(AnnouncementHandler handler);
    void onBrightTrustProposal(AnnouncementHandler handler);
    void onBrightTrustVote(AnnouncementHandler handler);

    // Lifecycle
    void start(boost::asio::io_context& ioc);
    void stop();
    void flushAnnouncements();

    // Inspection
    [[nodiscard]] std::vector<BlockAnnouncement> getPendingAnnouncements() const;
    [[nodiscard]] const GossipConfig& getConfig() const;

    // For testing: set local user IDs for recipient matching
    void setLocalUserIds(const std::set<std::string>& userIds);

private:
    void queueAnnouncement(BlockAnnouncement announcement);
    void batchFlush();
    void forwardAnnouncement(const BlockAnnouncement& announcement);
    std::vector<std::string> selectRandomPeers(int count);
    void encryptAndSendBatch(const std::string& peerId,
                             const std::vector<BlockAnnouncement>& batch);
    void scheduleBatchTimer();

    PeerManager& peerManager_;
    DiskBlockStore& blockStore_;
    db::HeadRegistry& headRegistry_;
    const Member& localNode_;
    GossipConfig config_;

    mutable std::shared_mutex announcementMutex_;
    std::vector<BlockAnnouncement> pendingAnnouncements_;
    std::unique_ptr<boost::asio::steady_timer> batchTimer_;

    std::vector<AnnouncementHandler> announcementHandlers_;
    std::vector<AnnouncementHandler> messageDeliveryHandlers_;
    std::vector<AnnouncementHandler> deliveryAckHandlers_;
    std::vector<AnnouncementHandler> brightTrustProposalHandlers_;
    std::vector<AnnouncementHandler> brightTrustVoteHandlers_;

    std::set<std::string> localUserIds_;
    bool running_ = false;
};

} // namespace brightchain::gossip
