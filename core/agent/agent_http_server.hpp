#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <memory>
#include <string>
#include <atomic>
#include "core/common/config.hpp"
#include "core/agent/node_identity.hpp"
#include "core/membership/membership_manager.hpp"
#include "core/registry/service_registry.hpp"
#include "core/config_manager/config_manager.hpp"
#include "core/health/health_monitor.hpp"
#include "core/networking/udp_transport.hpp"
#include "core/membership/failure_detector.hpp"

namespace dicp {

class AgentHttpServer : public std::enable_shared_from_this<AgentHttpServer> {
public:
    AgentHttpServer(boost::asio::io_context& ioc,
                    const AgentConfig& config,
                    NodeIdentity& identity,
                    std::shared_ptr<MembershipManager> membership_mgr,
                    std::shared_ptr<ServiceRegistry> registry,
                    std::shared_ptr<ConfigurationManager> config_mgr,
                    std::shared_ptr<HealthMonitor> health_monitor,
                    std::shared_ptr<UdpTransport> transport,
                    std::shared_ptr<FailureDetector> failure_detector);
    ~AgentHttpServer();

    void Start();
    void Stop();

    uint16_t LocalPort() const;

private:
    void StartAccept();
    void HandleSession(std::shared_ptr<boost::beast::tcp_stream> stream);
    std::string GeneratePrometheusMetrics() const;

    boost::asio::io_context& ioc_;
    const AgentConfig& config_;
    NodeIdentity& identity_;
    std::shared_ptr<MembershipManager> membership_mgr_;
    std::shared_ptr<ServiceRegistry> registry_;
    std::shared_ptr<ConfigurationManager> config_mgr_;
    std::shared_ptr<HealthMonitor> health_monitor_;
    std::shared_ptr<UdpTransport> transport_;
    std::shared_ptr<FailureDetector> failure_detector_;

    boost::asio::ip::tcp::acceptor acceptor_;
    std::atomic<bool> running_{false};
};

} // namespace dicp
