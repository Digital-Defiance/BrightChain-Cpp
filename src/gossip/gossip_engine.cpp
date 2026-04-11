#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/disk_block_store.hpp>
#include <brightchain/head_registry.hpp>
#include <brightchain/member.hpp>
#include <brightchain/ecies.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace brightchain::gossip {

// ── Helpers ────────────────────────────────────────────────────────────────

static std::string nowIso8601() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) %
              1000;
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count() << 'Z';
    return oss.str();
}

// ── Construction ───────────────────────────────────────────────────────────

GossipEngine::GossipEngine(
    PeerManager& peerManager,
    DiskBlockStore& blockStore,
    db::HeadRegistry& headRegistry,
    const Member& localNode,
    GossipConfig config)
    : peerManager_(peerManager),
      blockStore_(blockStore),
      headRegistry_(headRegistry),
      localNode_(localNode),
      config_(std::move(config)) {}

// ── Block announcements ────────────────────────────────────────────────────

void GossipEngine::announceBlock(const std::string& blockId,
                                 std::optional<std::string> poolId) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = blockId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.poolId = std::move(poolId);
    queueAnnouncement(std::move(ann));
}

void GossipEngine::announceRemoval(const std::string& blockId,
                                   std::optional<std::string> poolId) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Remove;
    ann.blockId = blockId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.poolId = std::move(poolId);
    queueAnnouncement(std::move(ann));
}

// ── Message delivery ───────────────────────────────────────────────────────

void GossipEngine::announceMessage(const std::vector<std::string>& blockIds,
                                   const MessageDeliveryMetadata& metadata) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Add;
    ann.blockId = metadata.cblBlockId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.messageDelivery = metadata;

    // Priority-based TTL and fanout
    if (metadata.priority == "high") {
        ann.ttl = config_.messagePriority.high.ttl;
    } else {
        ann.ttl = config_.messagePriority.normal.ttl;
    }

    queueAnnouncement(std::move(ann));
}

void GossipEngine::sendDeliveryAck(const DeliveryAckMetadata& ack) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::Ack;
    ann.blockId = ack.messageId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.deliveryAck = ack;
    queueAnnouncement(std::move(ann));
}

// ── Head registry sync ────────────────────────────────────────────────────

void GossipEngine::announceHeadUpdate(const std::string& dbName,
                                      const std::string& collectionName,
                                      const std::string& blockId,
                                      std::optional<WriteProof> proof) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::HeadUpdate;
    ann.blockId = blockId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.headUpdate = HeadUpdateMetadata{dbName, collectionName};
    ann.writeProof = std::move(proof);
    queueAnnouncement(std::move(ann));
}

// ── Base64 helpers (pool metadata encryption) ─────────────────────────────

static const char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static std::string base64Encode(const std::vector<uint8_t>& bytes) {
    std::string ret;
    ret.reserve((bytes.size() + 2) / 3 * 4);
    int i = 0;
    unsigned char c3[3];
    unsigned char c4[4];
    size_t idx = 0;
    while (idx < bytes.size()) {
        c3[i++] = bytes[idx++];
        if (i == 3) {
            c4[0] = (c3[0] & 0xfc) >> 2;
            c4[1] = ((c3[0] & 0x03) << 4) + ((c3[1] & 0xf0) >> 4);
            c4[2] = ((c3[1] & 0x0f) << 2) + ((c3[2] & 0xc0) >> 6);
            c4[3] = c3[2] & 0x3f;
            for (int j = 0; j < 4; j++) ret += kBase64Chars[c4[j]];
            i = 0;
        }
    }
    if (i > 0) {
        for (int j = i; j < 3; j++) c3[j] = 0;
        c4[0] = (c3[0] & 0xfc) >> 2;
        c4[1] = ((c3[0] & 0x03) << 4) + ((c3[1] & 0xf0) >> 4);
        c4[2] = ((c3[1] & 0x0f) << 2) + ((c3[2] & 0xc0) >> 6);
        for (int j = 0; j <= i; j++) ret += kBase64Chars[c4[j]];
        while (i++ < 3) ret += '=';
    }
    return ret;
}

// ── Pool lifecycle ─────────────────────────────────────────────────────────

void GossipEngine::announcePoolCreation(const std::string& poolId,
                                        const PoolAnnouncementMetadata& meta) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::PoolAnnounce;
    ann.blockId = poolId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.poolId = poolId;
    ann.poolAnnouncement = meta;

    // Req 8.5: When pool has encryption enabled, ECIES-encrypt pool metadata
    // so that only authorized members can read pool details.
    if (meta.encrypted && !ann.poolAnnouncement->encryptedMetadata.has_value()) {
        try {
            // Serialize pool metadata to JSON for encryption
            nlohmann::json metaJson;
            metaJson["blockCount"] = meta.blockCount;
            metaJson["totalSize"] = meta.totalSize;
            metaJson["encrypted"] = meta.encrypted;
            std::string plaintext = metaJson.dump();
            std::vector<uint8_t> plaintextBytes(plaintext.begin(), plaintext.end());

            // Encrypt with the local node's public key (the pool creator)
            auto pubKey = localNode_.publicKey();
            auto encrypted = Ecies::encryptBasic(plaintextBytes, pubKey);
            ann.poolAnnouncement->encryptedMetadata = base64Encode(encrypted);
        } catch (const std::exception& e) {
            std::cerr << "Warning: failed to encrypt pool metadata for pool "
                      << poolId << ": " << e.what() << "\n";
            // Proceed without encrypted metadata
        }
    }

    queueAnnouncement(std::move(ann));
}

void GossipEngine::announcePoolRemoval(const std::string& poolId) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::PoolRemove;
    ann.blockId = poolId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.poolId = poolId;
    queueAnnouncement(std::move(ann));
}

void GossipEngine::announcePoolDeletion(const std::string& poolId) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::PoolDeleted;
    ann.blockId = poolId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.poolId = poolId;
    queueAnnouncement(std::move(ann));
}

// ── BrightTrust ────────────────────────────────────────────────────────────

void GossipEngine::announceBrightTrustProposal(const QuorumProposalMetadata& meta) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumProposal;
    ann.blockId = meta.proposalId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.messagePriority.high.ttl;
    ann.quorumProposal = meta;
    queueAnnouncement(std::move(ann));
}

void GossipEngine::announceBrightTrustVote(const QuorumVoteMetadata& meta) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::QuorumVote;
    ann.blockId = meta.proposalId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.messagePriority.high.ttl;
    ann.quorumVote = meta;
    queueAnnouncement(std::move(ann));
}

// ── CBL index ──────────────────────────────────────────────────────────────

void GossipEngine::announceCblIndexUpdate(const std::string& poolId,
                                          const CblIndexEntry& entry) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::CblIndexUpdate;
    ann.blockId = entry.blockId1;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.poolId = poolId;
    ann.cblIndexEntry = entry;
    queueAnnouncement(std::move(ann));
}

void GossipEngine::announceCblIndexDelete(const std::string& poolId,
                                          const CblIndexEntry& entry) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::CblIndexDelete;
    ann.blockId = entry.blockId1;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.poolId = poolId;
    ann.cblIndexEntry = entry;
    queueAnnouncement(std::move(ann));
}

// ── ACL ────────────────────────────────────────────────────────────────────

void GossipEngine::announceAclUpdate(const std::string& poolId,
                                     const std::string& aclBlockId) {
    BlockAnnouncement ann;
    ann.type = AnnouncementType::AclUpdate;
    ann.blockId = aclBlockId;
    ann.nodeId = localNode_.idHex();
    ann.timestamp = nowIso8601();
    ann.ttl = config_.defaultTtl;
    ann.poolId = poolId;
    ann.aclBlockId = aclBlockId;
    queueAnnouncement(std::move(ann));
}

// ── Incoming announcement processing ───────────────────────────────────────

void GossipEngine::handleAnnouncement(const BlockAnnouncement& announcement) {
    // Validate the announcement; discard silently if invalid
    if (!announcement.validate()) {
        return;
    }

    // Notify all registered announcement handlers
    for (const auto& handler : announcementHandlers_) {
        handler(announcement);
    }

    bool shouldForward = true;

    // Type-specific handling
    switch (announcement.type) {
    case AnnouncementType::Add:
        if (announcement.messageDelivery.has_value()) {
            // Message delivery: check if any recipientIds match localUserIds_
            const auto& recipients = announcement.messageDelivery->recipientIds;
            bool localMatch = std::any_of(
                recipients.begin(), recipients.end(),
                [this](const std::string& rid) {
                    return localUserIds_.count(rid) > 0;
                });

            if (localMatch) {
                // Deliver locally
                for (const auto& handler : messageDeliveryHandlers_) {
                    handler(announcement);
                }

                // Auto-ack if required
                if (announcement.messageDelivery->ackRequired) {
                    // Find the first matching local user for the ack
                    std::string matchedRecipient;
                    for (const auto& rid : recipients) {
                        if (localUserIds_.count(rid) > 0) {
                            matchedRecipient = rid;
                            break;
                        }
                    }

                    DeliveryAckMetadata ack;
                    ack.messageId = announcement.messageDelivery->messageId;
                    ack.recipientId = matchedRecipient;
                    ack.status = "delivered";
                    ack.originalSenderNode = announcement.nodeId;
                    sendDeliveryAck(ack);
                }

                // Don't forward locally-matched messages
                shouldForward = false;
            }
        }
        break;

    case AnnouncementType::Ack:
        if (announcement.deliveryAck.has_value()) {
            for (const auto& handler : deliveryAckHandlers_) {
                handler(announcement);
            }
        }
        break;

    case AnnouncementType::QuorumProposal:
        for (const auto& handler : brightTrustProposalHandlers_) {
            handler(announcement);
        }
        break;

    case AnnouncementType::QuorumVote:
        for (const auto& handler : brightTrustVoteHandlers_) {
            handler(announcement);
        }
        break;

    case AnnouncementType::HeadUpdate:
        if (announcement.headUpdate.has_value()) {
            // If a writeProof is present, verify the ECDSA signature before applying
            if (announcement.writeProof.has_value()) {
                const auto& proof = *announcement.writeProof;

                // Reconstruct the signed message: dbName + collectionName + blockId
                std::string signedData = proof.dbName + proof.collectionName + proof.blockId;
                std::vector<uint8_t> dataBytes(signedData.begin(), signedData.end());

                // Decode hex public key to bytes
                std::vector<uint8_t> pubKeyBytes;
                pubKeyBytes.reserve(proof.signerPublicKey.size() / 2);
                for (size_t i = 0; i + 1 < proof.signerPublicKey.size(); i += 2) {
                    pubKeyBytes.push_back(static_cast<uint8_t>(
                        std::stoi(proof.signerPublicKey.substr(i, 2), nullptr, 16)));
                }

                // Decode hex signature to bytes
                std::vector<uint8_t> sigBytes;
                sigBytes.reserve(proof.signature.size() / 2);
                for (size_t i = 0; i + 1 < proof.signature.size(); i += 2) {
                    sigBytes.push_back(static_cast<uint8_t>(
                        std::stoi(proof.signature.substr(i, 2), nullptr, 16)));
                }

                // Verify — discard announcement if signature is invalid (Req 7.4)
                if (!Member::verifySignature(dataBytes, sigBytes, pubKeyBytes)) {
                    break;
                }
            }

            // Apply to HeadRegistry (Req 7.2)
            std::string key = announcement.headUpdate->dbName + ":" +
                              announcement.headUpdate->collectionName;
            headRegistry_.setHead(key, announcement.blockId);
        }
        break;

    case AnnouncementType::PoolAnnounce:
        // Update PeerManager pool cache
        if (announcement.poolId.has_value() && announcement.poolAnnouncement.has_value()) {
            peerManager_.updatePoolCache(
                *announcement.poolId,
                *announcement.poolAnnouncement,
                announcement.nodeId);
        }
        break;

    case AnnouncementType::PoolDeleted:
        // Remove from PeerManager pool cache
        if (announcement.poolId.has_value()) {
            peerManager_.removePoolFromCache(*announcement.poolId);
        }
        break;

    default:
        break;
    }

    // Forward if TTL > 0 (except for locally-matched messages)
    if (shouldForward && announcement.ttl > 0) {
        forwardAnnouncement(announcement);
    }
}

// ── Event subscriptions ────────────────────────────────────────────────────

void GossipEngine::onAnnouncement(AnnouncementHandler handler) {
    announcementHandlers_.push_back(std::move(handler));
}

void GossipEngine::onMessageDelivery(AnnouncementHandler handler) {
    messageDeliveryHandlers_.push_back(std::move(handler));
}

void GossipEngine::onDeliveryAck(AnnouncementHandler handler) {
    deliveryAckHandlers_.push_back(std::move(handler));
}

void GossipEngine::onBrightTrustProposal(AnnouncementHandler handler) {
    brightTrustProposalHandlers_.push_back(std::move(handler));
}

void GossipEngine::onBrightTrustVote(AnnouncementHandler handler) {
    brightTrustVoteHandlers_.push_back(std::move(handler));
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

void GossipEngine::start(boost::asio::io_context& ioc) {
    if (running_) return;
    running_ = true;
    batchTimer_ = std::make_unique<boost::asio::steady_timer>(ioc);
    scheduleBatchTimer();
}

void GossipEngine::stop() {
    running_ = false;
    if (batchTimer_) {
        batchTimer_->cancel();
        batchTimer_.reset();
    }
    // Flush any remaining announcements
    batchFlush();
}

void GossipEngine::flushAnnouncements() {
    batchFlush();
}

// ── Inspection ─────────────────────────────────────────────────────────────

std::vector<BlockAnnouncement> GossipEngine::getPendingAnnouncements() const {
    std::shared_lock lock(announcementMutex_);
    return pendingAnnouncements_;
}

const GossipConfig& GossipEngine::getConfig() const {
    return config_;
}

void GossipEngine::setLocalUserIds(const std::set<std::string>& userIds) {
    localUserIds_ = userIds;
}

// ── Private: queue and batch ───────────────────────────────────────────────

void GossipEngine::queueAnnouncement(BlockAnnouncement announcement) {
    std::unique_lock lock(announcementMutex_);
    pendingAnnouncements_.push_back(std::move(announcement));
}

void GossipEngine::batchFlush() {
    std::vector<BlockAnnouncement> batch;
    {
        std::unique_lock lock(announcementMutex_);
        batch.swap(pendingAnnouncements_);
    }

    if (batch.empty()) return;

    // Get connected peers once for the entire flush
    auto peerIds = peerManager_.getConnectedPeerIds();
    if (peerIds.empty()) return;

    // Process in chunks of maxBatchSize
    const auto maxBatch = static_cast<size_t>(config_.maxBatchSize);
    for (size_t offset = 0; offset < batch.size(); offset += maxBatch) {
        size_t end = std::min(offset + maxBatch, batch.size());
        std::vector<BlockAnnouncement> chunk(
            std::make_move_iterator(batch.begin() + static_cast<ptrdiff_t>(offset)),
            std::make_move_iterator(batch.begin() + static_cast<ptrdiff_t>(end)));

        // Determine fanout per announcement and group by target peer
        for (const auto& ann : chunk) {
            int fanout = config_.fanout;
            if (ann.messageDelivery.has_value()) {
                fanout = (ann.messageDelivery->priority == "high")
                             ? config_.messagePriority.high.fanout
                             : config_.messagePriority.normal.fanout;
            } else if (ann.type == AnnouncementType::QuorumProposal ||
                       ann.type == AnnouncementType::QuorumVote) {
                fanout = config_.messagePriority.high.fanout;
            }

            auto targets = selectRandomPeers(fanout);
            for (const auto& peerId : targets) {
                encryptAndSendBatch(peerId, {ann});
            }
        }
    }
}

void GossipEngine::scheduleBatchTimer() {
    if (!running_ || !batchTimer_) return;

    batchTimer_->expires_after(
        std::chrono::milliseconds(config_.batchIntervalMs));
    batchTimer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec || !running_) return;
        batchFlush();
        scheduleBatchTimer();
    });
}

// ── Private: forwarding ────────────────────────────────────────────────────

void GossipEngine::forwardAnnouncement(const BlockAnnouncement& announcement) {
    if (announcement.ttl <= 0) return;

    // Create a copy with decremented TTL
    BlockAnnouncement forwarded = announcement;
    forwarded.ttl = announcement.ttl - 1;

    // Determine fanout based on type/priority
    int fanout = config_.fanout;
    if (forwarded.messageDelivery.has_value()) {
        fanout = (forwarded.messageDelivery->priority == "high")
                     ? config_.messagePriority.high.fanout
                     : config_.messagePriority.normal.fanout;
    } else if (forwarded.type == AnnouncementType::QuorumProposal ||
               forwarded.type == AnnouncementType::QuorumVote) {
        fanout = config_.messagePriority.high.fanout;
    }

    auto targets = selectRandomPeers(fanout);

    // Check if batch contains sensitive metadata requiring encryption
    bool sensitive = forwarded.messageDelivery.has_value() ||
                     forwarded.deliveryAck.has_value();

    for (const auto& peerId : targets) {
        if (sensitive) {
            encryptAndSendBatch(peerId, {forwarded});
        } else {
            // Send as plaintext JSON
            nlohmann::json batchJson = nlohmann::json::array();
            batchJson.push_back(forwarded.toJson());
            peerManager_.sendToPeer(peerId, batchJson.dump());
        }
    }
}

std::vector<std::string> GossipEngine::selectRandomPeers(int count) {
    auto allPeers = peerManager_.getConnectedPeerIds();
    if (allPeers.empty()) return {};

    // Fisher-Yates shuffle
    static thread_local std::mt19937 rng(std::random_device{}());
    for (int i = static_cast<int>(allPeers.size()) - 1; i > 0; --i) {
        std::uniform_int_distribution<int> dist(0, i);
        std::swap(allPeers[static_cast<size_t>(i)],
                  allPeers[static_cast<size_t>(dist(rng))]);
    }

    // Take min(count, allPeers.size())
    auto n = std::min(static_cast<size_t>(count), allPeers.size());
    allPeers.resize(n);
    return allPeers;
}

void GossipEngine::encryptAndSendBatch(
    const std::string& peerId,
    const std::vector<BlockAnnouncement>& batch) {

    // Serialize the batch to JSON
    nlohmann::json batchJson = nlohmann::json::array();
    for (const auto& ann : batch) {
        batchJson.push_back(ann.toJson());
    }
    std::string payload = batchJson.dump();

    // Check if any announcement in the batch has sensitive metadata
    bool hasSensitive = std::any_of(batch.begin(), batch.end(),
        [](const BlockAnnouncement& ann) {
            return ann.messageDelivery.has_value() ||
                   ann.deliveryAck.has_value();
        });

    if (hasSensitive) {
        // Try to encrypt with peer's public key
        auto peerInfo = peerManager_.getPeer(peerId);
        if (peerInfo.has_value() && !peerInfo->publicKey.empty()) {
            try {
                std::vector<uint8_t> plaintext(payload.begin(), payload.end());
                auto encrypted = Ecies::encryptBasic(plaintext, peerInfo->publicKey);

                // Send as base64-encoded encrypted payload wrapped in JSON
                // The receiving peer will detect the encryption envelope
                nlohmann::json envelope;
                envelope["encrypted"] = true;
                envelope["data"] = nlohmann::json::array();
                for (auto byte : encrypted) {
                    envelope["data"].push_back(byte);
                }
                peerManager_.sendToPeer(peerId, envelope.dump());
                return;
            } catch (const std::exception& e) {
                // Fall through to plaintext with warning
                std::cerr << "ECIES encryption failed for peer " << peerId
                          << ": " << e.what() << " — sending plaintext\n";
            }
        } else {
            std::cerr << "Warning: no public key for peer " << peerId
                      << " — sending sensitive batch in plaintext\n";
        }
    }

    // Plaintext fallback
    peerManager_.sendToPeer(peerId, payload);
}

} // namespace brightchain::gossip
