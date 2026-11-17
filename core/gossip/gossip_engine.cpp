#include "gossip_engine.hpp"
#include "core/common/uuid.hpp"
#include "core/common/logger.hpp"

namespace dicp {

GossipEngine::GossipEngine(boost::asio::io_context& ioc,
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
      timer_(ioc) {}

GossipEngine::~GossipEngine() {
    Stop();
}

void GossipEngine::Start() {
    if (running_.exchange(true)) return;
    ScheduleNextRound();
}

void GossipEngine::Stop() {
    if (!running_.exchange(false)) return;
    boost::system::error_code ec;
    timer_.cancel(ec);
}

void GossipEngine::ScheduleNextRound() {
    if (!running_) return;

    timer_.expires_after(std::chrono::milliseconds(config_.gossip.interval_ms));
    auto self = shared_from_this();
    timer_.async_wait([this, self](boost::system::error_code ec) {
        if (!ec && running_) {
            ExecuteRound();
            ScheduleNextRound();
        }
    });
}

void GossipEngine::ExecuteRound() {
    rounds_executed_++;

    auto peers = membership_mgr_->GetRandomPeers(config_.gossip.fanout);
    if (peers.empty()) {
        return; // No peers yet
    }

    size_t overhead = 120; // envelope header overhead
    size_t max_payload = (config_.gossip.max_message_bytes > overhead) ?
                         (config_.gossip.max_message_bytes - overhead) : 500;

    auto batch = queue_->GetBatch(max_payload);
    if (batch.node_updates_size() == 0 &&
        batch.service_updates_size() == 0 &&
        batch.config_updates_size() == 0) {
        return; // Nothing to disseminate
    }

    proto::Envelope env;
    env.set_protocol_version(1);
    env.set_sender_node_id(identity_.NodeId());
    env.set_message_id(GenerateUUID());
    env.set_sender_sequence(rounds_executed_.load());
    env.set_type(proto::MSG_GOSSIP_BATCH);
    *env.mutable_gossip_batch() = batch;

    for (const auto& peer : peers) {
        transport_->SendTo(env, peer.advertised_address(), static_cast<uint16_t>(peer.gossip_port()));
    }
}

bool GossipEngine::IsDuplicate(const std::string& message_id) {
    if (message_id.empty()) return false;
    std::lock_guard<std::mutex> lock(dedup_mutex_);
    if (seen_messages_.find(message_id) != seen_messages_.end()) {
        return true;
    }

    seen_messages_.insert(message_id);
    seen_order_.push_back(message_id);

    if (seen_order_.size() > MAX_SEEN_CACHE) {
        seen_messages_.erase(seen_order_.front());
        seen_order_.pop_front();
    }
    return false;
}

void GossipEngine::HandleGossipBatch(const proto::Envelope& envelope) {
    if (IsDuplicate(envelope.message_id())) {
        return; // Suppress duplicate
    }

    const auto& batch = envelope.gossip_batch();

    // Process node updates
    for (int i = 0; i < batch.node_updates_size(); ++i) {
        const auto& up = batch.node_updates(i);
        auto [changed, refutation] = membership_mgr_->ApplyUpdate(up);

        if (refutation) {
            // We refuted a false suspicion/death assertion! Re-broadcast refutation immediately
            queue_->Enqueue(*refutation);
        } else if (changed) {
            // Propagate the new update to our peers
            queue_->Enqueue(up);

            // If a newly discovered peer joined us, respond immediately with our known cluster view
            if (up.node_id() == envelope.sender_node_id() && up.status() == proto::NODE_ALIVE) {
                proto::Envelope reply_env;
                reply_env.set_protocol_version(1);
                reply_env.set_sender_node_id(identity_.NodeId());
                reply_env.set_message_id(GenerateUUID());
                reply_env.set_type(proto::MSG_GOSSIP_BATCH);

                *reply_env.mutable_gossip_batch()->add_node_updates() = identity_.ToUpdate();
                auto alive = membership_mgr_->GetAliveMembers(true);
                for (const auto& m : alive) {
                    if (m.node_id() != up.node_id()) {
                        proto::NodeUpdate other;
                        other.set_node_id(m.node_id());
                        other.set_incarnation(m.incarnation());
                        other.set_status(m.status());
                        other.set_advertised_address(m.advertised_address());
                        other.set_gossip_port(m.gossip_port());
                        other.set_sync_port(m.sync_port());
                        other.set_management_port(m.management_port());
                        *reply_env.mutable_gossip_batch()->add_node_updates() = other;
                    }
                }
                transport_->SendTo(reply_env, up.advertised_address(), static_cast<uint16_t>(up.gossip_port()));
            }
        } else if (up.node_id() == envelope.sender_node_id() && up.status() == proto::NODE_ALIVE) {
            auto current_rec = membership_mgr_->GetMember(up.node_id());
            if (current_rec && (current_rec->status() == proto::NODE_SUSPECT || current_rec->status() == proto::NODE_DEAD)) {
                proto::Envelope refute_prompt;
                refute_prompt.set_protocol_version(1);
                refute_prompt.set_sender_node_id(identity_.NodeId());
                refute_prompt.set_message_id(GenerateUUID());
                refute_prompt.set_type(proto::MSG_GOSSIP_BATCH);

                proto::NodeUpdate claim;
                claim.set_node_id(current_rec->node_id());
                claim.set_incarnation(current_rec->incarnation());
                claim.set_status(current_rec->status());
                *refute_prompt.mutable_gossip_batch()->add_node_updates() = claim;

                transport_->SendTo(refute_prompt, up.advertised_address(), static_cast<uint16_t>(up.gossip_port()));
            }
        }
    }
}

void GossipEngine::BroadcastUpdate(const proto::NodeUpdate& update) {
    queue_->Enqueue(update);
}

} // namespace dicp
