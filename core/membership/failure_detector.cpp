#include "failure_detector.hpp"
#include "core/common/uuid.hpp"
#include "core/common/logger.hpp"

namespace dicp {

FailureDetector::FailureDetector(boost::asio::io_context& ioc,
                                 const AgentConfig& config,
                                 NodeIdentity& identity,
                                 std::shared_ptr<MembershipManager> membership_mgr,
                                 std::shared_ptr<DisseminationQueue> queue,
                                 std::shared_ptr<UdpTransport> transport)
    : ioc_(ioc),
      config_(config),
      identity_(identity),
      membership_mgr_(membership_mgr),
      queue_(queue),
      transport_(transport),
      probe_cycle_timer_(ioc) {}

FailureDetector::~FailureDetector() {
    Stop();
}

void FailureDetector::Start() {
    if (running_.exchange(true)) return;
    ScheduleNextProbe();
}

void FailureDetector::Stop() {
    if (!running_.exchange(false)) return;
    boost::system::error_code ec;
    probe_cycle_timer_.cancel(ec);

    std::lock_guard<std::mutex> lock(probes_mutex_);
    for (auto& [id, probe] : active_probes_) {
        if (probe->timeout_timer) {
            probe->timeout_timer->cancel(ec);
        }
    }
    active_probes_.clear();

    for (auto& [id, susp] : active_suspicions_) {
        if (susp->timer) {
            susp->timer->cancel(ec);
        }
    }
    active_suspicions_.clear();
}

void FailureDetector::ScheduleNextProbe() {
    if (!running_) return;

    probe_cycle_timer_.expires_after(std::chrono::milliseconds(config_.failure.probe_interval_ms));
    auto self = shared_from_this();
    probe_cycle_timer_.async_wait([this, self](boost::system::error_code ec) {
        if (!ec && running_) {
            StartProbeCycle();
            ScheduleNextProbe();
        }
    });
}

void FailureDetector::StartProbeCycle() {
    auto candidates = membership_mgr_->GetAliveMembers(true);
    if (candidates.empty()) return;

    // Pick random peer
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<size_t> dis(0, candidates.size() - 1);
    const auto& target = candidates[dis(gen)];

    std::string probe_id = GenerateUUID();
    auto probe = std::make_shared<ActiveProbe>();
    probe->probe_id = probe_id;
    probe->target_node_id = target.node_id();
    probe->target_incarnation = target.incarnation();
    probe->target_address = target.advertised_address();
    probe->target_port = static_cast<uint16_t>(target.gossip_port());
    probe->timeout_timer = std::make_shared<boost::asio::steady_timer>(ioc_);

    {
        std::lock_guard<std::mutex> lock(probes_mutex_);
        active_probes_[probe_id] = probe;
    }

    // Send direct PING
    proto::Envelope env;
    env.set_protocol_version(1);
    env.set_sender_node_id(identity_.NodeId());
    env.set_message_id(GenerateUUID());
    env.set_type(proto::MSG_PING);

    auto* ping = env.mutable_ping();
    ping->set_probe_id(probe_id);
    ping->set_target_node_id(target.node_id());
    ping->set_sender_incarnation(identity_.Incarnation());

    probes_sent_++;
    transport_->SendTo(env, probe->target_address, probe->target_port);

    // Direct timeout timer
    probe->timeout_timer->expires_after(std::chrono::milliseconds(config_.failure.probe_timeout_ms));
    auto self = shared_from_this();
    probe->timeout_timer->async_wait([this, self, probe_id](boost::system::error_code ec) {
        if (!ec && running_) {
            OnDirectTimeout(probe_id);
        }
    });
}

void FailureDetector::OnDirectTimeout(const std::string& probe_id) {
    std::shared_ptr<ActiveProbe> probe;
    {
        std::lock_guard<std::mutex> lock(probes_mutex_);
        auto it = active_probes_.find(probe_id);
        if (it == active_probes_.end()) return;
        probe = it->second;
    }

    if (probe->acked) return; // Already direct ACKed

    probe->direct_done = true;
    DICP_LOG_DEBUG("FailureDetector", "Direct probe " + probe_id + " timed out for target " + 
                   probe->target_node_id + ". Initiating indirect probes.");

    StartIndirectProbes(probe);
}

void FailureDetector::StartIndirectProbes(std::shared_ptr<ActiveProbe> probe) {
    auto intermediaries = membership_mgr_->GetRandomPeers(config_.failure.indirect_probe_count, probe->target_node_id);
    if (intermediaries.empty()) {
        // No intermediaries available to help, declare failure immediately
        OnProbeFailed(probe);
        return;
    }

    proto::Envelope req_env;
    req_env.set_protocol_version(1);
    req_env.set_sender_node_id(identity_.NodeId());
    req_env.set_message_id(GenerateUUID());
    req_env.set_type(proto::MSG_PING_REQ);

    auto* ping_req = req_env.mutable_ping_req();
    ping_req->set_probe_id(probe->probe_id);
    ping_req->set_target_node_id(probe->target_node_id);
    ping_req->set_target_address(probe->target_address);
    ping_req->set_target_gossip_port(probe->target_port);

    for (const auto& inter : intermediaries) {
        transport_->SendTo(req_env, inter.advertised_address(), static_cast<uint16_t>(inter.gossip_port()));
    }

    // Wait for indirect probe responses
    probe->timeout_timer->expires_after(std::chrono::milliseconds(config_.failure.probe_timeout_ms));
    auto self = shared_from_this();
    probe->timeout_timer->async_wait([this, self, probe](boost::system::error_code ec) {
        if (!ec && running_) {
            if (!probe->acked) {
                OnProbeFailed(probe);
            }
        }
    });
}

void FailureDetector::OnProbeFailed(std::shared_ptr<ActiveProbe> probe) {
    {
        std::lock_guard<std::mutex> lock(probes_mutex_);
        active_probes_.erase(probe->probe_id);
    }

    DICP_LOG_WARN("FailureDetector", "Probe failed for node " + probe->target_node_id + 
                  " (both direct and indirect failed). Declaring SUSPECT.");

    suspicions_declared_++;

    // Mark suspect in membership manager
    proto::NodeUpdate update;
    update.set_node_id(probe->target_node_id);
    update.set_incarnation(probe->target_incarnation);
    update.set_status(proto::NODE_SUSPECT);
    update.set_advertised_address(probe->target_address);
    update.set_gossip_port(probe->target_port);

    membership_mgr_->ApplyUpdate(update);
    queue_->Enqueue(update);

    // Start suspicion timer
    auto susp = std::make_shared<SuspicionRecord>();
    susp->node_id = probe->target_node_id;
    susp->incarnation = probe->target_incarnation;
    susp->timer = std::make_shared<boost::asio::steady_timer>(ioc_);

    {
        std::lock_guard<std::mutex> lock(probes_mutex_);
        active_suspicions_[probe->target_node_id] = susp;
    }

    susp->timer->expires_after(std::chrono::milliseconds(config_.failure.suspicion_timeout_ms));
    auto self = shared_from_this();
    std::string target_id = probe->target_node_id;
    uint64_t target_inc = probe->target_incarnation;

    susp->timer->async_wait([this, self, target_id, target_inc](boost::system::error_code ec) {
        if (!ec && running_) {
            OnSuspicionTimeout(target_id, target_inc);
        }
    });
}

void FailureDetector::OnSuspicionTimeout(const std::string& node_id, uint64_t incarnation) {
    {
        std::lock_guard<std::mutex> lock(probes_mutex_);
        auto it = active_suspicions_.find(node_id);
        if (it != active_suspicions_.end() && it->second->incarnation == incarnation) {
            active_suspicions_.erase(it);
        } else {
            return; // Suspicion was refuted or replaced
        }
    }

    auto member_opt = membership_mgr_->GetMember(node_id);
    if (!member_opt) return;

    if (member_opt->status() == proto::NODE_SUSPECT && member_opt->incarnation() == incarnation) {
        deaths_declared_++;
        DICP_LOG_WARN("FailureDetector", "Suspicion timer expired without refutation. Declaring node " + 
                      node_id + " DEAD.");

        membership_mgr_->MarkDeadIfSuspect(node_id, incarnation);

        proto::NodeUpdate death_update;
        death_update.set_node_id(node_id);
        death_update.set_incarnation(incarnation);
        death_update.set_status(proto::NODE_DEAD);
        queue_->Enqueue(death_update);
    }
}

void FailureDetector::HandlePing(const proto::Envelope& env, const boost::asio::ip::udp::endpoint& sender) {
    proto::Envelope ack_env;
    ack_env.set_protocol_version(1);
    ack_env.set_sender_node_id(identity_.NodeId());
    ack_env.set_message_id(GenerateUUID());
    ack_env.set_type(proto::MSG_ACK);

    auto* ack = ack_env.mutable_ack();
    ack->set_probe_id(env.ping().probe_id());
    ack->set_responder_node_id(identity_.NodeId());
    ack->set_responder_incarnation(identity_.Incarnation());

    transport_->SendTo(ack_env, sender);
}

void FailureDetector::HandleAck(const proto::Envelope& env) {
    const auto& ack = env.ack();
    std::shared_ptr<ActiveProbe> probe;
    {
        std::lock_guard<std::mutex> lock(probes_mutex_);
        auto it = active_probes_.find(ack.probe_id());
        if (it != active_probes_.end()) {
            probe = it->second;
            probe->acked = true;
            active_probes_.erase(it);
        }
    }

    if (probe) {
        if (!probe->direct_done) {
            direct_acks_++;
        }
        boost::system::error_code ec;
        if (probe->timeout_timer) {
            probe->timeout_timer->cancel(ec);
        }
        DICP_LOG_DEBUG("FailureDetector", "Received ACK for probe " + ack.probe_id() + 
                       " from " + ack.responder_node_id());
    }
}

void FailureDetector::HandlePingReq(const proto::Envelope& env, const boost::asio::ip::udp::endpoint& sender) {
    (void)sender;
    const auto& req = env.ping_req();
    std::string probe_id = req.probe_id();
    std::string requester_node_id = env.sender_node_id();

    DICP_LOG_DEBUG("FailureDetector", "Relaying indirect PING for target " + req.target_node_id() +
                   " requested by " + requester_node_id);

    // Send direct PING to the target
    proto::Envelope ping_env;
    ping_env.set_protocol_version(1);
    ping_env.set_sender_node_id(identity_.NodeId());
    ping_env.set_message_id(GenerateUUID());
    ping_env.set_type(proto::MSG_PING);

    auto* ping = ping_env.mutable_ping();
    ping->set_probe_id(probe_id);
    ping->set_target_node_id(req.target_node_id());
    ping->set_sender_incarnation(identity_.Incarnation());

    transport_->SendTo(ping_env, req.target_address(), static_cast<uint16_t>(req.target_gossip_port()));
}

void FailureDetector::HandleAckRelay(const proto::Envelope& env) {
    const auto& relay = env.ack_relay();
    if (!relay.success()) return;

    std::shared_ptr<ActiveProbe> probe;
    {
        std::lock_guard<std::mutex> lock(probes_mutex_);
        auto it = active_probes_.find(relay.probe_id());
        if (it != active_probes_.end()) {
            probe = it->second;
            probe->acked = true;
            active_probes_.erase(it);
        }
    }

    if (probe) {
        indirect_acks_++;
        boost::system::error_code ec;
        if (probe->timeout_timer) {
            probe->timeout_timer->cancel(ec);
        }
        DICP_LOG_DEBUG("FailureDetector", "Received indirect ACK_RELAY for probe " + relay.probe_id() +
                       " target " + relay.target_node_id());
    }
}

} // namespace dicp
