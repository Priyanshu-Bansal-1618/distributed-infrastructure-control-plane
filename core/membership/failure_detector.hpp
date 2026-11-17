#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <unordered_map>
#include <string>
#include <mutex>
#include <atomic>
#include "core/common/config.hpp"
#include "core/membership/membership_manager.hpp"
#include "core/networking/udp_transport.hpp"
#include "core/gossip/dissemination_queue.hpp"

namespace dicp {

class FailureDetector : public std::enable_shared_from_this<FailureDetector> {
public:
    FailureDetector(boost::asio::io_context& ioc,
                    const AgentConfig& config,
                    NodeIdentity& identity,
                    std::shared_ptr<MembershipManager> membership_mgr,
                    std::shared_ptr<DisseminationQueue> queue,
                    std::shared_ptr<UdpTransport> transport);
    ~FailureDetector();

    void Start();
    void Stop();

    // Message handlers for incoming probes
    void HandlePing(const proto::Envelope& env, const boost::asio::ip::udp::endpoint& sender);
    void HandleAck(const proto::Envelope& env);
    void HandlePingReq(const proto::Envelope& env, const boost::asio::ip::udp::endpoint& sender);
    void HandleAckRelay(const proto::Envelope& env);

    uint64_t ProbesSent() const { return probes_sent_.load(); }
    uint64_t DirectAcks() const { return direct_acks_.load(); }
    uint64_t IndirectAcks() const { return indirect_acks_.load(); }
    uint64_t SuspicionsDeclared() const { return suspicions_declared_.load(); }
    uint64_t DeathsDeclared() const { return deaths_declared_.load(); }

private:
    struct ActiveProbe {
        std::string probe_id;
        std::string target_node_id;
        uint64_t target_incarnation;
        std::string target_address;
        uint16_t target_port;
        bool direct_done{false};
        bool acked{false};
        std::shared_ptr<boost::asio::steady_timer> timeout_timer;
    };

    struct SuspicionRecord {
        std::string node_id;
        uint64_t incarnation;
        std::shared_ptr<boost::asio::steady_timer> timer;
    };

    void ScheduleNextProbe();
    void StartProbeCycle();
    void OnDirectTimeout(const std::string& probe_id);
    void StartIndirectProbes(std::shared_ptr<ActiveProbe> probe);
    void OnProbeFailed(std::shared_ptr<ActiveProbe> probe);
    void OnSuspicionTimeout(const std::string& node_id, uint64_t incarnation);

    boost::asio::io_context& ioc_;
    const AgentConfig& config_;
    NodeIdentity& identity_;
    std::shared_ptr<MembershipManager> membership_mgr_;
    std::shared_ptr<DisseminationQueue> queue_;
    std::shared_ptr<UdpTransport> transport_;

    boost::asio::steady_timer probe_cycle_timer_;
    std::atomic<bool> running_{false};

    mutable std::mutex probes_mutex_;
    std::unordered_map<std::string, std::shared_ptr<ActiveProbe>> active_probes_;
    std::unordered_map<std::string, std::shared_ptr<SuspicionRecord>> active_suspicions_;

    std::atomic<uint64_t> probes_sent_{0};
    std::atomic<uint64_t> direct_acks_{0};
    std::atomic<uint64_t> indirect_acks_{0};
    std::atomic<uint64_t> suspicions_declared_{0};
    std::atomic<uint64_t> deaths_declared_{0};
};

} // namespace dicp
