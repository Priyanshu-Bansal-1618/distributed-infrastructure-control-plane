#pragma once

#include <boost/asio.hpp>
#include <memory>
#include <unordered_set>
#include <deque>
#include <mutex>
#include "core/common/config.hpp"
#include "core/membership/membership_manager.hpp"
#include "core/networking/udp_transport.hpp"
#include "dissemination_queue.hpp"

namespace dicp {

class GossipEngine : public std::enable_shared_from_this<GossipEngine> {
public:
    GossipEngine(boost::asio::io_context& ioc,
                 const AgentConfig& config,
                 NodeIdentity& identity,
                 std::shared_ptr<MembershipManager> membership_mgr,
                 std::shared_ptr<DisseminationQueue> queue,
                 std::shared_ptr<UdpTransport> transport);
    ~GossipEngine();

    void Start();
    void Stop();

    void HandleGossipBatch(const proto::Envelope& envelope);
    void BroadcastUpdate(const proto::NodeUpdate& update);

    uint64_t RoundsExecuted() const { return rounds_executed_.load(); }

private:
    void ScheduleNextRound();
    void ExecuteRound();
    bool IsDuplicate(const std::string& message_id);

    boost::asio::io_context& ioc_;
    const AgentConfig& config_;
    NodeIdentity& identity_;
    std::shared_ptr<MembershipManager> membership_mgr_;
    std::shared_ptr<DisseminationQueue> queue_;
    std::shared_ptr<UdpTransport> transport_;

    boost::asio::steady_timer timer_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> rounds_executed_{0};

    mutable std::mutex dedup_mutex_;
    std::unordered_set<std::string> seen_messages_;
    std::deque<std::string> seen_order_;
    static constexpr size_t MAX_SEEN_CACHE = 5000;
};

} // namespace dicp
