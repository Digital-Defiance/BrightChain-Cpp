#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>

namespace brightchain::gossip {

struct UpnpConfig {
    bool enabled = false;
    uint16_t httpPort = 3000;
    uint16_t websocketPort = 3000;
    int ttlSeconds = 3600;
    int refreshIntervalMs = 1800000;
    int retryAttempts = 3;
    int retryDelayMs = 5000;

    bool operator==(const UpnpConfig& other) const = default;

    nlohmann::json toJson() const;
    static UpnpConfig fromJson(const nlohmann::json& j);

    /// Returns true iff the config is valid.
    /// Validates: refreshIntervalMs < ttlSeconds * 1000.
    static bool validate(const UpnpConfig& config);
};

class UpnpManager {
public:
    explicit UpnpManager(const UpnpConfig& config);

    /// Discover IGD, get external IP, create port mappings.
    /// Uses boost::asio::steady_timer for refresh scheduling.
    void initialize(boost::asio::io_context& ioc);

    /// Remove all mappings and stop refresh timer.
    void shutdown();

    [[nodiscard]] std::optional<std::string> getExternalIp() const;
    [[nodiscard]] uint16_t getMappedHttpPort() const;
    [[nodiscard]] uint16_t getMappedWsPort() const;
    [[nodiscard]] const UpnpConfig& getConfig() const;

    /// Compute retry delay for a given attempt (0-indexed):
    /// retryDelayMs * 2^attempt.  Public for testing.
    [[nodiscard]] int calculateRetryDelay(int attempt) const;

private:
    void createMapping(uint16_t internalPort, uint16_t externalPort,
                       const std::string& desc);
    void removeAllMappings();
    void refreshMappings();
    void scheduleRefreshTimer();

    UpnpConfig config_;
    std::string externalIp_;
    bool initialized_ = false;
    std::unique_ptr<boost::asio::steady_timer> refreshTimer_;
    bool running_ = false;

    /// Track which port mappings we created so we can remove them.
    struct PortMapping {
        uint16_t externalPort;
        std::string protocol; // "TCP"
    };
    std::vector<PortMapping> activeMappings_;
};

} // namespace brightchain::gossip
