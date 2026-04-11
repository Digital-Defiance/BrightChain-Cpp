#include <brightchain/gossip/upnp_manager.hpp>

#include <cmath>
#include <stdexcept>
#include <thread>

// miniupnpc is optional — guard includes behind feature detection.
#ifdef HAVE_MINIUPNPC
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

namespace brightchain::gossip {

// ── UpnpConfig serialization ───────────────────────────────────────────────

nlohmann::json UpnpConfig::toJson() const {
    return {
        {"enabled", enabled},
        {"httpPort", httpPort},
        {"websocketPort", websocketPort},
        {"ttlSeconds", ttlSeconds},
        {"refreshIntervalMs", refreshIntervalMs},
        {"retryAttempts", retryAttempts},
        {"retryDelayMs", retryDelayMs},
    };
}

UpnpConfig UpnpConfig::fromJson(const nlohmann::json& j) {
    UpnpConfig c;
    c.enabled = j.at("enabled").get<bool>();
    c.httpPort = j.at("httpPort").get<uint16_t>();
    c.websocketPort = j.at("websocketPort").get<uint16_t>();
    c.ttlSeconds = j.at("ttlSeconds").get<int>();
    c.refreshIntervalMs = j.at("refreshIntervalMs").get<int>();
    c.retryAttempts = j.at("retryAttempts").get<int>();
    c.retryDelayMs = j.at("retryDelayMs").get<int>();
    return c;
}

bool UpnpConfig::validate(const UpnpConfig& config) {
    // refreshIntervalMs must be strictly less than ttlSeconds * 1000
    // to ensure mappings are refreshed before they expire.
    if (config.ttlSeconds <= 0) return false;
    if (config.refreshIntervalMs <= 0) return false;
    if (config.retryAttempts < 0) return false;
    if (config.retryDelayMs < 0) return false;
    return config.refreshIntervalMs < config.ttlSeconds * 1000;
}

// ── Construction ───────────────────────────────────────────────────────────

UpnpManager::UpnpManager(const UpnpConfig& config)
    : config_(config) {}

// ── calculateRetryDelay ────────────────────────────────────────────────────

int UpnpManager::calculateRetryDelay(int attempt) const {
    if (attempt <= 0) return config_.retryDelayMs;
    double delay = static_cast<double>(config_.retryDelayMs)
                 * std::pow(2.0, static_cast<double>(attempt));
    return static_cast<int>(delay);
}

// ── Queries ────────────────────────────────────────────────────────────────

std::optional<std::string> UpnpManager::getExternalIp() const {
    if (!initialized_ || externalIp_.empty()) return std::nullopt;
    return externalIp_;
}

uint16_t UpnpManager::getMappedHttpPort() const {
    return config_.httpPort;
}

uint16_t UpnpManager::getMappedWsPort() const {
    return config_.websocketPort;
}

const UpnpConfig& UpnpManager::getConfig() const {
    return config_;
}

// ── initialize ─────────────────────────────────────────────────────────────

void UpnpManager::initialize(boost::asio::io_context& ioc) {
    if (!config_.enabled) return;

    if (!UpnpConfig::validate(config_)) {
        throw std::invalid_argument(
            "Invalid UpnpConfig: refreshIntervalMs must be < ttlSeconds * 1000");
    }

#ifdef HAVE_MINIUPNPC
    // Discover IGD (Internet Gateway Device)
    int error = 0;
    UPNPDev* devlist = upnpDiscover(2000, nullptr, nullptr, 0, 0, 2, &error);
    if (!devlist) {
        // No UPnP device found — non-fatal, log and continue.
        return;
    }

    UPNPUrls urls;
    IGDdatas data;
    char lanAddr[64] = {};

    int status = UPNP_GetValidIGD(devlist, &urls, &data, lanAddr, sizeof(lanAddr));
    freeUPNPDevlist(devlist);

    if (status != 1) {
        // No valid IGD found.
        return;
    }

    // Get external IP address.
    char externalIp[40] = {};
    int r = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIp);
    if (r == 0) {
        externalIp_ = externalIp;
    }

    // Create port mappings with retry logic.
    auto ttlStr = std::to_string(config_.ttlSeconds);

    auto tryCreateMapping = [&](uint16_t port, const std::string& desc) {
        auto portStr = std::to_string(port);
        for (int attempt = 0; attempt <= config_.retryAttempts; ++attempt) {
            int result = UPNP_AddPortMapping(
                urls.controlURL, data.first.servicetype,
                portStr.c_str(), portStr.c_str(), lanAddr,
                desc.c_str(), "TCP", nullptr, ttlStr.c_str());
            if (result == 0) {
                activeMappings_.push_back({port, "TCP"});
                return true;
            }
            // Exponential backoff before retry (blocking — acceptable during init).
            if (attempt < config_.retryAttempts) {
                int delayMs = calculateRetryDelay(attempt);
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
        }
        return false;
    };

    tryCreateMapping(config_.httpPort, "BrightChain HTTP");
    if (config_.websocketPort != config_.httpPort) {
        tryCreateMapping(config_.websocketPort, "BrightChain WebSocket");
    }

    FreeUPNPUrls(&urls);
#endif // HAVE_MINIUPNPC

    initialized_ = true;

    // Start refresh timer.
    refreshTimer_ = std::make_unique<boost::asio::steady_timer>(ioc);
    running_ = true;
    scheduleRefreshTimer();
}

// ── shutdown ───────────────────────────────────────────────────────────────

void UpnpManager::shutdown() {
    running_ = false;
    if (refreshTimer_) {
        refreshTimer_->cancel();
        refreshTimer_.reset();
    }

    removeAllMappings();
    initialized_ = false;
}

// ── createMapping ──────────────────────────────────────────────────────────

void UpnpManager::createMapping(uint16_t /*internalPort*/, uint16_t externalPort,
                                const std::string& /*desc*/) {
    // Individual mapping creation — used by refreshMappings.
    // In a full implementation this would call UPNP_AddPortMapping.
    activeMappings_.push_back({externalPort, "TCP"});
}

// ── removeAllMappings ──────────────────────────────────────────────────────

void UpnpManager::removeAllMappings() {
#ifdef HAVE_MINIUPNPC
    // In a full implementation, we would re-discover the IGD and call
    // UPNP_DeletePortMapping for each active mapping.
    // For now, best-effort: mappings expire naturally after TTL.
#endif
    activeMappings_.clear();
}

// ── refreshMappings ────────────────────────────────────────────────────────

void UpnpManager::refreshMappings() {
    if (!initialized_ || !config_.enabled) return;

    // Re-create port mappings to refresh their TTL.
    // In a full implementation this would call UPNP_AddPortMapping again.
#ifdef HAVE_MINIUPNPC
    // Re-discover IGD and refresh mappings.
    // Simplified: the initialize() logic would be factored out and reused.
#endif
}

// ── scheduleRefreshTimer ───────────────────────────────────────────────────

void UpnpManager::scheduleRefreshTimer() {
    if (!running_ || !refreshTimer_) return;

    refreshTimer_->expires_after(
        std::chrono::milliseconds(config_.refreshIntervalMs));
    refreshTimer_->async_wait([this](const boost::system::error_code& ec) {
        if (ec || !running_) return;
        refreshMappings();
        scheduleRefreshTimer();
    });
}

} // namespace brightchain::gossip
