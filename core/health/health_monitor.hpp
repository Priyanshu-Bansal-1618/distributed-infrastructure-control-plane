#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>
#include "core/common/config.hpp"
#include "core/registry/service_registry.hpp"

namespace dicp {

struct HealthCheckConfig {
    std::string service_name;
    std::string instance_id;
    std::string host;
    uint16_t port;
    std::string path{"/health"};
    uint32_t interval_seconds{5};
    uint32_t timeout_ms{2000};
    uint32_t failure_threshold{3};
    uint32_t recovery_threshold{2};
};

struct HealthCheckState {
    HealthCheckConfig config;
    proto::HealthStatus current_health{proto::HEALTH_UNKNOWN};
    uint32_t consecutive_failures{0};
    uint32_t consecutive_successes{0};
    uint32_t last_status_code{0};
    uint64_t last_latency_ms{0};
    uint64_t last_check_time_sec{0};
    std::shared_ptr<boost::asio::steady_timer> check_timer;
};

class HealthMonitor : public std::enable_shared_from_this<HealthMonitor> {
public:
    HealthMonitor(boost::asio::io_context& ioc,
                  const AgentConfig& config,
                  std::shared_ptr<ServiceRegistry> registry);
    ~HealthMonitor();

    void Start();
    void Stop();

    void AddCheck(const HealthCheckConfig& check_cfg);
    void RemoveCheck(const std::string& service_name, const std::string& instance_id);

    std::optional<HealthCheckState> GetState(const std::string& service_name, const std::string& instance_id) const;

    uint64_t TotalChecksExecuted() const { return checks_executed_.load(); }
    uint64_t TotalChecksPassed() const { return checks_passed_.load(); }
    uint64_t TotalChecksFailed() const { return checks_failed_.load(); }

private:
    void ScheduleCheck(std::shared_ptr<HealthCheckState> state);
    void ExecuteCheck(std::shared_ptr<HealthCheckState> state);

    boost::asio::io_context& ioc_;
    const AgentConfig& config_;
    std::shared_ptr<ServiceRegistry> registry_;

    std::atomic<bool> running_{false};
    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<HealthCheckState>> checks_;

    std::atomic<uint64_t> checks_executed_{0};
    std::atomic<uint64_t> checks_passed_{0};
    std::atomic<uint64_t> checks_failed_{0};
};

} // namespace dicp
