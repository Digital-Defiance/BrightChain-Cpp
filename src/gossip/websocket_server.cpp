#include <brightchain/gossip/websocket_server.hpp>
#include <brightchain/gossip/gossip_engine.hpp>
#include <brightchain/gossip/peer_manager.hpp>
#include <brightchain/ecies.hpp>
#include <brightchain/member.hpp>

#include <boost/asio/strand.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <random>
#include <set>

// jwt-cpp is header-only; conditionally include if available.
#if __has_include(<jwt-cpp/jwt.h>)
#include <jwt-cpp/jwt.h>
#define HAS_JWT_CPP 1
#else
#define HAS_JWT_CPP 0
#endif

namespace brightchain::gossip {

// ── Free functions ─────────────────────────────────────────────────────────

EventAccessTier getAccessTierForEvent(const std::string& eventType) {
    if (eventType == "peer:connected" ||
        eventType == "peer:disconnected" ||
        eventType == "storage:alert") {
        return EventAccessTier::Admin;
    }
    if (eventType == "pool:changed" ||
        eventType == "pool:created" ||
        eventType == "pool:deleted") {
        return EventAccessTier::PoolScoped;
    }
    if (eventType == "energy:updated") {
        return EventAccessTier::MemberScoped;
    }
    // Default to Admin (most restrictive) for unknown events.
    return EventAccessTier::Admin;
}

bool isClientAuthorizedForEvent(const JwtClaims& claims,
                                const WebSocketEvent& event) {
    switch (event.accessTier) {
    case EventAccessTier::Admin:
        // Only Admin or System members may receive admin events.
        return claims.memberType == MemberType::Admin ||
               claims.memberType == MemberType::System;

    case EventAccessTier::PoolScoped: {
        // Admin/System always have access.
        if (claims.memberType == MemberType::Admin ||
            claims.memberType == MemberType::System) {
            return true;
        }
        // Otherwise, the client needs a read permission on the target pool.
        if (!event.targetPoolId.has_value()) {
            return false;
        }
        const std::string requiredPerm =
            "pool:" + event.targetPoolId.value() + ":read";
        return std::find(claims.permissions.begin(),
                         claims.permissions.end(),
                         requiredPerm) != claims.permissions.end();
    }

    case EventAccessTier::MemberScoped:
        // Only the target member may receive member-scoped events.
        if (!event.targetMemberId.has_value()) {
            return false;
        }
        return claims.memberId == event.targetMemberId.value();
    }

    return false;
}

// ── Construction / Destruction ─────────────────────────────────────────────

WebSocketServer::WebSocketServer(boost::asio::io_context& ioc,
                                 GossipEngine& engine,
                                 PeerManager& peerManager,
                                 const Member& localNode,
                                 const std::string& jwtSecret)
    : ioc_(ioc)
    , engine_(engine)
    , peerManager_(peerManager)
    , localNode_(localNode)
    , jwtSecret_(jwtSecret) {}

WebSocketServer::~WebSocketServer() {
    stop();
}

// ── Lifecycle ──────────────────────────────────────────────────────────────

void WebSocketServer::start(const std::string& address, uint16_t port) {
    if (running_) return;
    running_ = true;

    auto endpoint = boost::asio::ip::tcp::endpoint(
        boost::asio::ip::make_address(address), port);

    acceptor_ = std::make_shared<boost::asio::ip::tcp::acceptor>(ioc_);
    boost::system::error_code ec;
    acceptor_->open(endpoint.protocol(), ec);
    if (ec) return;

    acceptor_->set_option(boost::asio::socket_base::reuse_address(true), ec);
    acceptor_->bind(endpoint, ec);
    if (ec) return;

    acceptor_->listen(boost::asio::socket_base::max_listen_connections, ec);
    if (ec) return;

    doAccept();

    heartbeatTimer_ = std::make_unique<boost::asio::steady_timer>(ioc_);
    scheduleHeartbeat();

    jwtExpiryTimer_ = std::make_unique<boost::asio::steady_timer>(ioc_);
    scheduleJwtExpiryCheck();
}

void WebSocketServer::stop() {
    running_ = false;

    if (acceptor_) {
        boost::system::error_code ec;
        acceptor_->close(ec);
        acceptor_.reset();
    }

    if (heartbeatTimer_) {
        heartbeatTimer_->cancel();
        heartbeatTimer_.reset();
    }

    if (jwtExpiryTimer_) {
        jwtExpiryTimer_->cancel();
        jwtExpiryTimer_.reset();
    }

    // Close all client WS streams.
    {
        std::unique_lock lock(wsStreamMutex_);
        for (auto& [id, ws] : clientWsStreams_) {
            if (ws && ws->is_open()) {
                boost::system::error_code ec2;
                ws->close(boost::beast::websocket::close_code::going_away, ec2);
            }
        }
        clientWsStreams_.clear();
    }

    std::unique_lock lock(clientMutex_);
    clientSessions_.clear();
}

// ── Accept loop ────────────────────────────────────────────────────────────

void WebSocketServer::doAccept() {
    if (!running_ || !acceptor_) return;

    acceptor_->async_accept(
        boost::asio::make_strand(ioc_),
        [this](boost::system::error_code ec,
               boost::asio::ip::tcp::socket socket) {
            if (ec || !running_) return;

            // Read the HTTP upgrade request to determine the target path.
            auto ws = std::make_shared<WsStream>(std::move(socket));

            auto buffer = std::make_shared<boost::beast::flat_buffer>();
            auto req = std::make_shared<
                boost::beast::http::request<boost::beast::http::string_body>>();

            boost::beast::http::async_read(
                boost::beast::get_lowest_layer(*ws), *buffer, *req,
                [this, ws, buffer, req](boost::system::error_code ec2,
                                        std::size_t /*bytes*/) {
                    if (ec2 || !running_) return;

                    std::string target(req->target());

                    // Route: /ws/node/:nodeId
                    const std::string nodePrefix = "/ws/node/";
                    if (target.substr(0, nodePrefix.size()) == nodePrefix) {
                        std::string nodeId = target.substr(nodePrefix.size());
                        ws->async_accept(*req,
                            [this, ws, nodeId](boost::system::error_code ec3) {
                                if (ec3 || !running_) return;
                                handleNodeConnection(ws, nodeId);
                            });
                        return;
                    }

                    // Route: /ws/client?token=<jwt>
                    const std::string clientPrefix = "/ws/client";
                    if (target.substr(0, clientPrefix.size()) == clientPrefix) {
                        // Extract token from query string.
                        std::string token;
                        auto qpos = target.find('?');
                        if (qpos != std::string::npos) {
                            std::string query = target.substr(qpos + 1);
                            const std::string tokenKey = "token=";
                            auto tpos = query.find(tokenKey);
                            if (tpos != std::string::npos) {
                                token = query.substr(tpos + tokenKey.size());
                                auto apos = token.find('&');
                                if (apos != std::string::npos) {
                                    token = token.substr(0, apos);
                                }
                            }
                        }

                        ws->async_accept(*req,
                            [this, ws, token](boost::system::error_code ec3) {
                                if (ec3 || !running_) return;
                                handleClientConnection(ws, token);
                            });
                        return;
                    }

                    // Unknown path — close.
                    boost::system::error_code closeEc;
                    boost::beast::get_lowest_layer(*ws).socket().close(closeEc);
                });

            // Continue accepting.
            doAccept();
        });
}

// ── Node connection handler ────────────────────────────────────────────────

void WebSocketServer::handleNodeConnection(std::shared_ptr<WsStream> ws,
                                           const std::string& nodeId) {
    // Node-to-node connections use ECIES challenge/response.
    auto peerOpt = peerManager_.getPeer(nodeId);
    if (!peerOpt.has_value() || peerOpt->publicKey.empty()) {
        boost::system::error_code ec;
        ws->close(boost::beast::websocket::close_code::policy_error, ec);
        return;
    }

    bool authOk = authenticateEcies(ws, peerOpt->publicKey);
    if (!authOk) {
        boost::system::error_code ec;
        ws->close(boost::beast::websocket::close_code::policy_error, ec);
        return;
    }

    // Authentication succeeded — start reading gossip messages.
    startNodeReadLoop(ws, nodeId);
}

// ── Client connection handler ──────────────────────────────────────────────

void WebSocketServer::handleClientConnection(std::shared_ptr<WsStream> ws,
                                             const std::string& token) {
    auto claimsOpt = validateJwt(token);
    if (!claimsOpt.has_value()) {
        boost::system::error_code ec;
        boost::beast::websocket::close_reason cr;
        cr.code = static_cast<boost::beast::websocket::close_code>(4001);
        cr.reason = "auth_failed";
        ws->close(cr, ec);
        return;
    }

    // Create a client session.
    ClientSession session;
    session.sessionId = "client-" + claimsOpt->memberId + "-" +
                        std::to_string(std::chrono::steady_clock::now()
                                           .time_since_epoch()
                                           .count());
    session.claims = claimsOpt.value();
    session.lastPongReceived = std::chrono::steady_clock::now();
    session.authenticated = true;
    session.tokenExpiringSent = false;

    std::string sessionId = session.sessionId;
    addClientSession(session);

    // Track the WS stream for this session.
    {
        std::unique_lock lock(wsStreamMutex_);
        clientWsStreams_[sessionId] = ws;
    }

    // Start reading subscription messages from the client.
    startClientReadLoop(ws, sessionId);
}

// ── Node read loop ─────────────────────────────────────────────────────────

void WebSocketServer::startNodeReadLoop(std::shared_ptr<WsStream> ws,
                                        const std::string& nodeId) {
    if (!running_) return;

    auto buffer = std::make_shared<boost::beast::flat_buffer>();
    ws->async_read(*buffer,
        [this, ws, buffer, nodeId](boost::system::error_code ec,
                                    std::size_t /*bytes*/) {
            if (ec || !running_) return;

            // Parse incoming gossip announcement and forward to engine.
            try {
                auto data = buffer->data();
                std::string msg(static_cast<const char*>(data.data()),
                                data.size());
                auto j = nlohmann::json::parse(msg);
                auto announcement = BlockAnnouncement::fromJson(j);
                engine_.handleAnnouncement(announcement);
            } catch (...) {
                // Malformed message — log and ignore per error handling spec.
            }

            buffer->consume(buffer->size());
            startNodeReadLoop(ws, nodeId);
        });
}

// ── Client read loop ───────────────────────────────────────────────────────

void WebSocketServer::startClientReadLoop(std::shared_ptr<WsStream> ws,
                                          const std::string& sessionId) {
    if (!running_) return;

    auto buffer = std::make_shared<boost::beast::flat_buffer>();
    ws->async_read(*buffer,
        [this, ws, buffer, sessionId](boost::system::error_code ec,
                                       std::size_t /*bytes*/) {
            if (ec || !running_) {
                // Connection closed — clean up.
                removeClientSession(sessionId);
                {
                    std::unique_lock lock(wsStreamMutex_);
                    clientWsStreams_.erase(sessionId);
                }
                return;
            }

            // Parse subscription commands: {"action":"subscribe","event":"pool:changed"}
            try {
                auto data = buffer->data();
                std::string msg(static_cast<const char*>(data.data()),
                                data.size());
                auto j = nlohmann::json::parse(msg);

                if (j.contains("action") && j.contains("event")) {
                    std::string action = j["action"].get<std::string>();
                    std::string eventType = j["event"].get<std::string>();

                    if (action == "subscribe") {
                        subscribeClient(sessionId, eventType);
                    } else if (action == "unsubscribe") {
                        unsubscribeClient(sessionId, eventType);
                    }
                }
            } catch (...) {
                // Malformed message — ignore.
            }

            buffer->consume(buffer->size());
            startClientReadLoop(ws, sessionId);
        });
}

// ── ECIES authentication ───────────────────────────────────────────────────

bool WebSocketServer::authenticateEcies(
    std::shared_ptr<WsStream> ws,
    const std::vector<uint8_t>& peerPublicKey) {

    // Generate a random 32-byte challenge.
    std::vector<uint8_t> challenge(32);
    std::random_device rd;
    for (auto& byte : challenge) {
        byte = static_cast<uint8_t>(rd());
    }

    // Encrypt with peer's public key.
    std::vector<uint8_t> encrypted;
    try {
        encrypted = Ecies::encryptBasic(challenge, peerPublicKey);
    } catch (...) {
        return false;
    }

    // Send encrypted challenge to peer.
    boost::system::error_code ec;
    ws->write(boost::asio::buffer(encrypted), ec);
    if (ec) return false;

    // Read peer's response (decrypted challenge).
    boost::beast::flat_buffer buffer;
    ws->read(buffer, ec);
    if (ec) return false;

    auto responseData = buffer.data();
    std::vector<uint8_t> response(
        static_cast<const uint8_t*>(responseData.data()),
        static_cast<const uint8_t*>(responseData.data()) + responseData.size());

    return response == challenge;
}

// ── Send to client ─────────────────────────────────────────────────────────

void WebSocketServer::sendToClient(const std::string& sessionId,
                                   const std::string& message) {
    std::shared_ptr<WsStream> ws;
    {
        std::shared_lock lock(wsStreamMutex_);
        auto it = clientWsStreams_.find(sessionId);
        if (it == clientWsStreams_.end()) return;
        ws = it->second;
    }

    if (!ws || !ws->is_open()) return;

    boost::system::error_code ec;
    ws->text(true);
    ws->write(boost::asio::buffer(message), ec);
    if (ec) {
        // Write failed — remove the dead session.
        removeClientSession(sessionId);
        std::unique_lock lock(wsStreamMutex_);
        clientWsStreams_.erase(sessionId);
    }
}

// ── Heartbeat ──────────────────────────────────────────────────────────────

void WebSocketServer::scheduleHeartbeat() {
    if (!running_ || !heartbeatTimer_) return;

    heartbeatTimer_->expires_after(
        std::chrono::milliseconds(heartbeatIntervalMs_));
    heartbeatTimer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec || !running_) return;

        auto now = std::chrono::steady_clock::now();
        std::vector<std::string> timedOut;

        // Send pings and check for pong timeouts.
        {
            std::shared_lock lock(wsStreamMutex_);
            std::shared_lock cLock(clientMutex_);
            for (auto& [sessionId, ws] : clientWsStreams_) {
                if (!ws || !ws->is_open()) continue;

                // Send ping.
                boost::system::error_code pingEc;
                ws->async_ping({},
                    [](boost::system::error_code) {});

                // Check pong timeout.
                auto it = clientSessions_.find(sessionId);
                if (it != clientSessions_.end()) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - it->second.lastPongReceived).count();
                    if (elapsed > heartbeatTimeoutMs_) {
                        timedOut.push_back(sessionId);
                    }
                }
            }
        }

        // Close timed-out sessions.
        for (const auto& sessionId : timedOut) {
            std::shared_ptr<WsStream> ws;
            {
                std::shared_lock lock(wsStreamMutex_);
                auto it = clientWsStreams_.find(sessionId);
                if (it != clientWsStreams_.end()) ws = it->second;
            }
            if (ws && ws->is_open()) {
                boost::system::error_code closeEc;
                ws->close(boost::beast::websocket::close_code::policy_error, closeEc);
            }
            removeClientSession(sessionId);
            {
                std::unique_lock lock(wsStreamMutex_);
                clientWsStreams_.erase(sessionId);
            }
        }

        scheduleHeartbeat();
    });
}

// ── JWT expiry check ───────────────────────────────────────────────────────

void WebSocketServer::scheduleJwtExpiryCheck() {
    if (!running_ || !jwtExpiryTimer_) return;

    jwtExpiryTimer_->expires_after(std::chrono::seconds(5));
    jwtExpiryTimer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec || !running_) return;

        auto now = std::chrono::system_clock::now();
        std::vector<std::string> expiredSessions;
        std::vector<std::string> expiringSessions;

        {
            std::shared_lock lock(clientMutex_);
            for (auto& [id, session] : clientSessions_) {
                auto timeUntilExpiry =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        session.claims.expiresAt - now)
                        .count();

                if (timeUntilExpiry <= 0) {
                    // Token expired past grace period — close with 4002.
                    expiredSessions.push_back(id);
                } else if (timeUntilExpiry <= jwtGracePeriodMs_ &&
                           !session.tokenExpiringSent) {
                    // Within grace period — send auth:token_expiring event.
                    expiringSessions.push_back(id);
                }
            }
        }

        // Send auth:token_expiring to sessions approaching expiry.
        for (const auto& sessionId : expiringSessions) {
            nlohmann::json expiringEvent;
            expiringEvent["event"] = "auth:token_expiring";
            expiringEvent["data"] = nlohmann::json::object();
            sendToClient(sessionId, expiringEvent.dump());

            // Mark as sent so we don't send again.
            std::unique_lock lock(clientMutex_);
            auto it = clientSessions_.find(sessionId);
            if (it != clientSessions_.end()) {
                it->second.tokenExpiringSent = true;
            }
        }

        // Close expired sessions with code 4002.
        for (const auto& sessionId : expiredSessions) {
            std::shared_ptr<WsStream> ws;
            {
                std::shared_lock lock(wsStreamMutex_);
                auto it = clientWsStreams_.find(sessionId);
                if (it != clientWsStreams_.end()) ws = it->second;
            }
            if (ws && ws->is_open()) {
                boost::system::error_code closeEc;
                boost::beast::websocket::close_reason cr;
                cr.code = static_cast<boost::beast::websocket::close_code>(
                    kCloseCodeJwtExpired);
                cr.reason = "token_expired";
                ws->close(cr, closeEc);
            }
            removeClientSession(sessionId);
            {
                std::unique_lock lock(wsStreamMutex_);
                clientWsStreams_.erase(sessionId);
            }
        }

        scheduleJwtExpiryCheck();
    });
}

// ── JWT validation ─────────────────────────────────────────────────────────

std::optional<JwtClaims> WebSocketServer::validateJwt(
    const std::string& token) const {
    if (token.empty()) return std::nullopt;

#if HAS_JWT_CPP
    try {
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{jwtSecret_})
            .with_issuer("brightchain");

        auto decoded = jwt::decode(token);
        verifier.verify(decoded);

        JwtClaims claims;
        if (decoded.has_payload_claim("memberId")) {
            claims.memberId = decoded.get_payload_claim("memberId").as_string();
        }
        if (decoded.has_payload_claim("memberType")) {
            int mt = decoded.get_payload_claim("memberType").as_integer();
            claims.memberType = static_cast<MemberType>(
                std::clamp(mt, 0, 3));
        }
        if (decoded.has_payload_claim("permissions")) {
            auto permClaim = decoded.get_payload_claim("permissions");
            auto permSet = permClaim.as_set();
            for (const auto& p : permSet) {
                claims.permissions.push_back(p);
            }
        }
        if (decoded.has_payload_claim("exp")) {
            auto exp = decoded.get_payload_claim("exp").as_date();
            claims.expiresAt = exp;
        }

        return claims;
    } catch (...) {
        return std::nullopt;
    }
#else
    // Without jwt-cpp we cannot verify the signature, so reject.
    (void)token;
    return std::nullopt;
#endif
}

std::string WebSocketServer::createJwt(const JwtClaims& claims) const {
#if HAS_JWT_CPP
    auto token = jwt::create()
        .set_issuer("brightchain")
        .set_type("JWT")
        .set_payload_claim("memberId", jwt::claim(claims.memberId))
        .set_payload_claim("memberType",
                           jwt::claim(std::to_string(static_cast<int>(claims.memberType))))
        .set_expires_at(claims.expiresAt);

    if (!claims.permissions.empty()) {
        std::set<std::string> permSet(claims.permissions.begin(),
                                      claims.permissions.end());
        token.set_payload_claim("permissions", jwt::claim(permSet));
    }

    return token.sign(jwt::algorithm::hs256{jwtSecret_});
#else
    (void)claims;
    return "";
#endif
}

// ── Event broadcasting ─────────────────────────────────────────────────────

void WebSocketServer::broadcastEvent(const WebSocketEvent& event) {
    // Collect authorized session IDs first, then send outside the lock.
    std::vector<std::string> targetSessions;

    {
        std::shared_lock lock(clientMutex_);
        for (const auto& [id, session] : clientSessions_) {
            // Check subscription.
            if (session.subscribedEvents.find(event.event) ==
                session.subscribedEvents.end()) {
                continue;
            }
            // Check authorization.
            if (!isClientAuthorizedForEvent(session.claims, event)) {
                continue;
            }
            targetSessions.push_back(id);
        }
    }

    // Serialize the event once.
    nlohmann::json eventJson;
    eventJson["event"] = event.event;
    eventJson["data"] = event.data;
    if (event.targetPoolId.has_value()) {
        eventJson["targetPoolId"] = event.targetPoolId.value();
    }
    if (event.targetMemberId.has_value()) {
        eventJson["targetMemberId"] = event.targetMemberId.value();
    }
    std::string message = eventJson.dump();

    // Send to each authorized, subscribed client.
    for (const auto& sessionId : targetSessions) {
        sendToClient(sessionId, message);
    }
}

// ── Session management ─────────────────────────────────────────────────────

void WebSocketServer::addClientSession(const ClientSession& session) {
    std::unique_lock lock(clientMutex_);
    clientSessions_[session.sessionId] = session;
}

void WebSocketServer::removeClientSession(const std::string& sessionId) {
    std::unique_lock lock(clientMutex_);
    clientSessions_.erase(sessionId);
}

std::optional<ClientSession> WebSocketServer::getClientSession(
    const std::string& sessionId) const {
    std::shared_lock lock(clientMutex_);
    auto it = clientSessions_.find(sessionId);
    if (it == clientSessions_.end()) return std::nullopt;
    return it->second;
}

void WebSocketServer::subscribeClient(const std::string& sessionId,
                                      const std::string& eventType) {
    std::unique_lock lock(clientMutex_);
    auto it = clientSessions_.find(sessionId);
    if (it != clientSessions_.end()) {
        it->second.subscribedEvents.insert(eventType);
    }
}

void WebSocketServer::unsubscribeClient(const std::string& sessionId,
                                        const std::string& eventType) {
    std::unique_lock lock(clientMutex_);
    auto it = clientSessions_.find(sessionId);
    if (it != clientSessions_.end()) {
        it->second.subscribedEvents.erase(eventType);
    }
}

std::vector<ClientSession> WebSocketServer::getClientSessions() const {
    std::shared_lock lock(clientMutex_);
    std::vector<ClientSession> result;
    result.reserve(clientSessions_.size());
    for (const auto& [id, session] : clientSessions_) {
        result.push_back(session);
    }
    return result;
}

} // namespace brightchain::gossip
