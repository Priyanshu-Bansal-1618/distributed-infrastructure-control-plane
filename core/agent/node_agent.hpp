#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include "core/common/config.hpp"
#include "core/common/logger.hpp"
#include "core/agent/node_identity.hpp"
#include "core/networking/udp_transport.hpp"
#include "core/networking/tcp_transport.hpp"
#include "core/membership/membership_manager.hpp"
#include "core/membership/failure_detector.hpp"
#include "core/gossip/dissemination_queue.hpp"
#include "core/gossip/gossip_engine.hpp"
#include "core/registry/service_registry.hpp"
#include "core/config_manager/config_manager.hpp"
#include "core/health/health_monitor.hpp"
#include "core/storage/sqlite_storage.hpp"

namespace dicp {

class AgentHttpServer;

class NodeAgent {
public:
    explicit NodeAgent(const AgentConfig& config);
    ~NodeAgent();

    void Start();
    void Stop();
    void WaitForShutdown();

    bool IsRunning() const { return running_.load(); }

    NodeIdentity& Identity() { return identity_; }
    const NodeIdentity& Identity() const { return identity_; }
    const AgentConfig& Config() const { return config_; }

    std::shared_ptr<MembershipManager> GetMembershipManager() const { return membership_mgr_; }
    std::shared_ptr<DisseminationQueue> GetDisseminationQueue() const { return dissemination_queue_; }
    std::shared_ptr<GossipEngine> GetGossipEngine() const { return gossip_engine_; }
    std::shared_ptr<FailureDetector> GetFailureDetector() const { return failure_detector_; }
    std::shared_ptr<ServiceRegistry> GetServiceRegistry() const { return registry_; }
    std::shared_ptr<ConfigurationManager> GetConfigManager() const { return config_mgr_; }
    std::shared_ptr<HealthMonitor> GetHealthMonitor() const { return health_monitor_; }
    std::shared_ptr<SqliteStorage> GetStorage() const { return storage_; }

    std::shared_ptr<UdpTransport> GetUdpTransport() const { return udp_transport_; }
    std::shared_ptr<TcpServer> GetTcpServer() const { return tcp_server_; }
    boost::asio::io_context& GetIoContext() { return ioc_; }

    void JoinSeed(const std::string& host, uint16_t port);

private:
    void HandleUdpMessage(const proto::Envelope& envelope, const boost::asio::ip::udp::endpoint& sender);
    std::optional<proto::Envelope> HandleTcpRequest(const proto::Envelope& request);
    void BootstrapSeeds();
    void ScheduleHousekeeping();
    void DoHousekeeping();

    AgentConfig config_;
    NodeIdentity identity_;
    boost::asio::io_context ioc_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;

    std::shared_ptr<UdpTransport> udp_transport_;
    std::shared_ptr<TcpServer> tcp_server_;

    std::shared_ptr<MembershipManager> membership_mgr_;
    std::shared_ptr<DisseminationQueue> dissemination_queue_;
    std::shared_ptr<GossipEngine> gossip_engine_;
    std::shared_ptr<FailureDetector> failure_detector_;
    std::shared_ptr<ServiceRegistry> registry_;
    std::shared_ptr<ConfigurationManager> config_mgr_;
    std::shared_ptr<HealthMonitor> health_monitor_;
    std::shared_ptr<SqliteStorage> storage_;
    std::shared_ptr<AgentHttpServer> http_server_;

    boost::asio::steady_timer housekeeping_timer_;
    std::vector<std::thread> thread_pool_;
    std::atomic<bool> running_{false};
};

} // namespace dicp
