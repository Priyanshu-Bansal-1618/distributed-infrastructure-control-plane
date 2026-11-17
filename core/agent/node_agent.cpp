#include "node_agent.hpp"
#include "agent_http_server.hpp"
#include "core/common/uuid.hpp"

namespace dicp {

NodeAgent::NodeAgent(const AgentConfig& config)
    : config_(config),
      identity_(config),
      ioc_(),
      work_guard_(boost::asio::make_work_guard(ioc_)),
      housekeeping_timer_(ioc_) {
    Logger::Instance().SetNodeId(identity_.NodeId());

    std::string db_path = config_.storage.database_path;
    if (db_path == "./data/node.db") {
        db_path = "./data/" + config_.node.id + ".db";
    }
    storage_ = std::make_shared<SqliteStorage>(db_path);

    udp_transport_ = std::make_shared<UdpTransport>(
        ioc_,
        config_.node.bind_address,
        config_.network.gossip_port,
        config_.gossip.max_message_bytes);

    tcp_server_ = std::make_shared<TcpServer>(
        ioc_,
        config_.node.bind_address,
        config_.network.sync_port);

    membership_mgr_ = std::make_shared<MembershipManager>(identity_);
    dissemination_queue_ = std::make_shared<DisseminationQueue>(
        config_.gossip.max_pending_updates,
        config_.gossip.fanout);

    registry_ = std::make_shared<ServiceRegistry>(
        config_,
        identity_,
        dissemination_queue_);

    config_mgr_ = std::make_shared<ConfigurationManager>(dissemination_queue_);

    health_monitor_ = std::make_shared<HealthMonitor>(
        ioc_,
        config_,
        registry_);

    gossip_engine_ = std::make_shared<GossipEngine>(
        ioc_,
        config_,
        identity_,
        membership_mgr_,
        dissemination_queue_,
        udp_transport_);

    failure_detector_ = std::make_shared<FailureDetector>(
        ioc_,
        config_,
        identity_,
        membership_mgr_,
        dissemination_queue_,
        udp_transport_);

    http_server_ = std::make_shared<AgentHttpServer>(
        ioc_,
        config_,
        identity_,
        membership_mgr_,
        registry_,
        config_mgr_,
        health_monitor_,
        udp_transport_,
        failure_detector_);

    udp_transport_->SetMessageHandler(
        [this](const proto::Envelope& env, const boost::asio::ip::udp::endpoint& sender) {
            HandleUdpMessage(env, sender);
        });

    tcp_server_->SetHandler(
        [this](const proto::Envelope& req) {
            return HandleTcpRequest(req);
        });
}

NodeAgent::~NodeAgent() {
    Stop();
}

void NodeAgent::Start() {
    if (running_.exchange(true)) return;

    DICP_LOG_INFO("NodeAgent", "Starting Node Agent " + identity_.NodeId() +
                  " (boot_id=" + identity_.BootId() + ", gossip_port=" +
                  std::to_string(config_.network.gossip_port) + ")");

    // Recover persisted nodes and own incarnation from SQLite
    try {
        auto saved_nodes = storage_->LoadNodes();
        for (const auto& n : saved_nodes) {
            if (n.node_id() == identity_.NodeId()) {
                identity_.SetIncarnationIfGreater(n.incarnation() + 1);
            } else {
                membership_mgr_->AddOrUpdateMember(n);
            }
        }
    } catch (...) {}

    storage_->SaveNode(identity_.ToRecord());

    udp_transport_->Start();
    tcp_server_->Start();
    http_server_->Start();
    gossip_engine_->Start();
    failure_detector_->Start();
    health_monitor_->Start();

    // Launch worker thread pool (2 threads)
    size_t num_threads = 2;
    for (size_t i = 0; i < num_threads; ++i) {
        thread_pool_.emplace_back([this]() {
            ioc_.run();
        });
    }

    identity_.SetStatus(proto::NODE_ALIVE);
    dissemination_queue_->Enqueue(identity_.ToUpdate());

    ScheduleHousekeeping();
    BootstrapSeeds();

    DICP_LOG_INFO("NodeAgent", "Node Agent started successfully in ALIVE state");
}

void NodeAgent::Stop() {
    if (!running_.exchange(false)) return;

    DICP_LOG_INFO("NodeAgent", "Stopping Node Agent " + identity_.NodeId() + " gracefully");
    identity_.SetStatus(proto::NODE_LEFT);

    boost::system::error_code ec;
    housekeeping_timer_.cancel(ec);

    // Broadcast departure
    auto leave_update = identity_.ToUpdate();
    leave_update.set_status(proto::NODE_LEFT);
    dissemination_queue_->Enqueue(leave_update);

    // Send departure gossip to known alive members
    auto peers = membership_mgr_->GetAliveMembers(true);
    proto::Envelope leave_env;
    leave_env.set_protocol_version(1);
    leave_env.set_sender_node_id(identity_.NodeId());
    leave_env.set_message_id(GenerateUUID());
    leave_env.set_type(proto::MSG_GOSSIP_BATCH);
    *leave_env.mutable_gossip_batch()->add_node_updates() = leave_update;

    for (const auto& peer : peers) {
        udp_transport_->SendTo(leave_env, peer.advertised_address(), static_cast<uint16_t>(peer.gossip_port()));
    }

    if (storage_) {
        storage_->SaveNode(identity_.ToRecord());
    }

    if (health_monitor_) health_monitor_->Stop();
    if (failure_detector_) failure_detector_->Stop();
    if (gossip_engine_) gossip_engine_->Stop();
    if (http_server_) http_server_->Stop();
    if (udp_transport_) udp_transport_->Stop();
    if (tcp_server_) tcp_server_->Stop();

    work_guard_.reset();
    ioc_.stop();

    for (auto& t : thread_pool_) {
        if (t.joinable()) {
            t.join();
        }
    }
    thread_pool_.clear();

    DICP_LOG_INFO("NodeAgent", "Node Agent stopped cleanly");
}

void NodeAgent::WaitForShutdown() {
    for (auto& t : thread_pool_) {
        if (t.joinable()) {
            t.join();
        }
    }
}

void NodeAgent::JoinSeed(const std::string& host, uint16_t port) {
    proto::Envelope env;
    env.set_protocol_version(1);
    env.set_sender_node_id(identity_.NodeId());
    env.set_message_id(GenerateUUID());
    env.set_type(proto::MSG_GOSSIP_BATCH);

    *env.mutable_gossip_batch()->add_node_updates() = identity_.ToUpdate();
    udp_transport_->SendTo(env, host, port);

    DICP_LOG_INFO("NodeAgent", "Sent bootstrap join announcement to seed " + host + ":" + std::to_string(port));
}

void NodeAgent::BootstrapSeeds() {
    for (const auto& seed : config_.seeds) {
        size_t colon = seed.find(':');
        if (colon != std::string::npos) {
            std::string host = seed.substr(0, colon);
            uint16_t port = static_cast<uint16_t>(std::stoi(seed.substr(colon + 1)));
            if (port != config_.network.gossip_port || host != config_.node.advertised_address) {
                JoinSeed(host, port);
            }
        }
    }
}

void NodeAgent::ScheduleHousekeeping() {
    if (!running_) return;

    housekeeping_timer_.expires_after(std::chrono::seconds(1));
    housekeeping_timer_.async_wait([this](boost::system::error_code ec) {
        if (!ec && running_) {
            DoHousekeeping();
            ScheduleHousekeeping();
        }
    });
}

void NodeAgent::DoHousekeeping() {
    if (registry_) {
        registry_->CheckLeaseExpirations();
        registry_->PurgeExpiredTombstones();
    }
    if (storage_) {
        storage_->SaveNode(identity_.ToRecord());
    }
}

void NodeAgent::HandleUdpMessage(const proto::Envelope& envelope, const boost::asio::ip::udp::endpoint& sender) {
    DICP_LOG_DEBUG("NodeAgent", "Received UDP message type=" + std::to_string(envelope.type()) +
                   " from " + sender.address().to_string() + ":" + std::to_string(sender.port()) +
                   " sender_node=" + envelope.sender_node_id());

    switch (envelope.type()) {
        case proto::MSG_PING:
            if (failure_detector_) {
                failure_detector_->HandlePing(envelope, sender);
            }
            break;
        case proto::MSG_ACK:
            if (failure_detector_) {
                failure_detector_->HandleAck(envelope);
            }
            break;
        case proto::MSG_PING_REQ:
            if (failure_detector_) {
                failure_detector_->HandlePingReq(envelope, sender);
            }
            break;
        case proto::MSG_ACK_RELAY:
            if (failure_detector_) {
                failure_detector_->HandleAckRelay(envelope);
            }
            break;
        case proto::MSG_GOSSIP_BATCH:
            if (gossip_engine_) {
                gossip_engine_->HandleGossipBatch(envelope);
            }
            if (registry_ && envelope.has_gossip_batch()) {
                const auto& batch = envelope.gossip_batch();
                for (int i = 0; i < batch.service_updates_size(); ++i) {
                    bool changed = registry_->ApplyServiceUpdate(batch.service_updates(i));
                    if (changed && dissemination_queue_) {
                        dissemination_queue_->Enqueue(batch.service_updates(i));
                    }
                }
                for (int i = 0; i < batch.config_updates_size(); ++i) {
                    bool changed = config_mgr_->ApplyConfigUpdate(batch.config_updates(i));
                    if (changed && dissemination_queue_) {
                        dissemination_queue_->Enqueue(batch.config_updates(i));
                    }
                }
            }
            break;
        default:
            DICP_LOG_WARN("NodeAgent", "Unknown or unhandled UDP message type: " + std::to_string(envelope.type()));
            break;
    }
}

std::optional<proto::Envelope> NodeAgent::HandleTcpRequest(const proto::Envelope& request) {
    DICP_LOG_DEBUG("NodeAgent", "Received TCP request type=" + std::to_string(request.type()) +
                   " from node " + request.sender_node_id());

    if (request.type() == proto::MSG_SYNC_REQUEST) {
        proto::Envelope resp;
        resp.set_protocol_version(1);
        resp.set_sender_node_id(identity_.NodeId());
        resp.set_message_id(GenerateUUID());
        resp.set_type(proto::MSG_SYNC_RESPONSE);

        auto* sync = resp.mutable_sync_payload();
        auto members = membership_mgr_->GetAllMembers();
        for (const auto& m : members) {
            *sync->add_nodes() = m;
        }

        auto services = registry_->GetAllInstances(true);
        for (const auto& s : services) {
            *sync->add_services() = s;
        }

        auto configs = config_mgr_->GetAll();
        for (const auto& c : configs) {
            *sync->add_configs() = c;
        }

        return resp;
    }
    return std::nullopt;
}

} // namespace dicp
