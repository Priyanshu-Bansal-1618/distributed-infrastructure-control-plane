#include "agent_http_server.hpp"
#include "core/common/logger.hpp"
#include "core/common/types.hpp"
#include <nlohmann/json.hpp>
#include <sstream>

namespace dicp {

namespace http = boost::beast::http;
using json = nlohmann::json;

AgentHttpServer::AgentHttpServer(boost::asio::io_context& ioc,
                                 const AgentConfig& config,
                                 NodeIdentity& identity,
                                 std::shared_ptr<MembershipManager> membership_mgr,
                                 std::shared_ptr<ServiceRegistry> registry,
                                 std::shared_ptr<ConfigurationManager> config_mgr,
                                 std::shared_ptr<HealthMonitor> health_monitor,
                                 std::shared_ptr<UdpTransport> transport,
                                 std::shared_ptr<FailureDetector> failure_detector)
    : ioc_(ioc),
      config_(config),
      identity_(identity),
      membership_mgr_(membership_mgr),
      registry_(registry),
      config_mgr_(config_mgr),
      health_monitor_(health_monitor),
      transport_(transport),
      failure_detector_(failure_detector),
      acceptor_(ioc) {
    boost::asio::ip::tcp::endpoint ep(
        boost::asio::ip::make_address(config_.node.bind_address),
        config_.network.management_port);

    acceptor_.open(ep.protocol());
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen();
}

AgentHttpServer::~AgentHttpServer() {
    Stop();
}

void AgentHttpServer::Start() {
    if (running_.exchange(true)) return;
    DICP_LOG_INFO("AgentHttpServer", "Starting Agent Management HTTP API on port " + std::to_string(LocalPort()));
    StartAccept();
}

void AgentHttpServer::Stop() {
    if (!running_.exchange(false)) return;
    boost::system::error_code ec;
    acceptor_.close(ec);
}

uint16_t AgentHttpServer::LocalPort() const {
    boost::system::error_code ec;
    return acceptor_.local_endpoint(ec).port();
}

void AgentHttpServer::StartAccept() {
    if (!running_) return;

    acceptor_.async_accept([this](boost::system::error_code ec, boost::asio::ip::tcp::socket socket) {
        if (!ec) {
            auto stream = std::make_shared<boost::beast::tcp_stream>(std::move(socket));
            HandleSession(stream);
        }
        if (running_) {
            StartAccept();
        }
    });
}

void AgentHttpServer::HandleSession(std::shared_ptr<boost::beast::tcp_stream> stream) {
    auto buffer = std::make_shared<boost::beast::flat_buffer>();
    auto req = std::make_shared<http::request<http::string_body>>();

    stream->expires_after(std::chrono::seconds(5));
    http::async_read(*stream, *buffer, *req, [this, stream, buffer, req](boost::beast::error_code ec, auto) {
        if (ec) return;

        auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req->version());
        res->set(http::field::server, "DICP-Agent/1.0");
        std::string target = std::string(req->target());

        if (target == "/metrics" && req->method() == http::verb::get) {
            res->set(http::field::content_type, "text/plain; version=0.0.4");
            res->body() = GeneratePrometheusMetrics();
        } else if (target == "/api/v1/agent/self" && req->method() == http::verb::get) {
            res->set(http::field::content_type, "application/json");
            json j;
            j["node_id"] = identity_.NodeId();
            j["boot_id"] = identity_.BootId();
            j["hostname"] = identity_.Hostname();
            j["advertised_address"] = identity_.AdvertisedAddress();
            j["gossip_port"] = identity_.GossipPort();
            j["sync_port"] = identity_.SyncPort();
            j["management_port"] = identity_.ManagementPort();
            j["incarnation"] = identity_.Incarnation();
            j["status"] = static_cast<int>(identity_.Status());
            j["record_version"] = identity_.RecordVersion();
            res->body() = j.dump(2);
        } else if (target == "/api/v1/agent/members" && req->method() == http::verb::get) {
            res->set(http::field::content_type, "application/json");
            json j = json::array();
            auto members = membership_mgr_->GetAllMembers();
            for (const auto& m : members) {
                j.push_back({
                    {"node_id", m.node_id()},
                    {"advertised_address", m.advertised_address()},
                    {"gossip_port", m.gossip_port()},
                    {"sync_port", m.sync_port()},
                    {"management_port", m.management_port()},
                    {"status", static_cast<int>(m.status())},
                    {"incarnation", m.incarnation()},
                    {"last_seen_ms", m.last_seen_unix_ms()}
                });
            }
            res->body() = j.dump(2);
        } else if (target == "/api/v1/agent/services" && req->method() == http::verb::get) {
            res->set(http::field::content_type, "application/json");
            json j = json::array();
            auto instances = registry_->GetAllInstances(false);
            for (const auto& inst : instances) {
                j.push_back({
                    {"instance_id", inst.instance_id()},
                    {"service_name", inst.service_name()},
                    {"owner_node_id", inst.owner_node_id()},
                    {"address", inst.address()},
                    {"port", inst.port()},
                    {"health", static_cast<int>(inst.health())},
                    {"weight", inst.weight()},
                    {"lease_expiry", inst.lease_expiry_unix_sec()}
                });
            }
            res->body() = j.dump(2);
        } else if (target.rfind("/api/v1/agent/services/", 0) == 0 && target.ends_with("/snapshot") && req->method() == http::verb::get) {
            // /api/v1/agent/services/{service_name}/snapshot
            std::string svc = target.substr(23, target.size() - 23 - 9);
            auto eligible = registry_->Discover(svc, true);
            json j;
            j["service_name"] = svc;
            j["version"] = CurrentTimeSec();
            j["endpoints"] = json::array();
            for (const auto& ep : eligible) {
                j["endpoints"].push_back({
                    {"instance_id", ep.instance_id()},
                    {"address", ep.address()},
                    {"port", ep.port()},
                    {"weight", ep.weight()}
                });
            }
            res->set(http::field::content_type, "application/json");
            res->body() = j.dump(2);
        } else if (target == "/api/v1/agent/services/register" && req->method() == http::verb::post) {
            try {
                auto body = json::parse(req->body());
                proto::ServiceInstanceRecord rec;
                rec.set_instance_id(body["instance_id"].get<std::string>());
                rec.set_service_name(body["service_name"].get<std::string>());
                rec.set_address(body["address"].get<std::string>());
                rec.set_port(body["port"].get<uint32_t>());
                rec.set_weight(body.value("weight", 1U));
                rec.set_health(proto::HEALTH_HEALTHY);

                bool ok = registry_->Register(rec);
                if (ok && body.contains("health_check_url")) {
                    std::string url = body["health_check_url"].get<std::string>();
                    HealthCheckConfig hcfg;
                    hcfg.service_name = rec.service_name();
                    hcfg.instance_id = rec.instance_id();
                    hcfg.host = rec.address();
                    hcfg.port = static_cast<uint16_t>(rec.port());
                    health_monitor_->AddCheck(hcfg);
                }

                res->set(http::field::content_type, "application/json");
                res->result(ok ? http::status::created : http::status::bad_request);
                res->body() = ok ? "{\"status\":\"registered\"}" : "{\"error\":\"registration failed\"}";
            } catch (...) {
                res->result(http::status::bad_request);
                res->body() = "{\"error\":\"invalid json payload\"}";
            }
        } else if (target == "/api/v1/agent/services/deregister" && req->method() == http::verb::post) {
            try {
                auto body = json::parse(req->body());
                std::string svc = body["service_name"].get<std::string>();
                std::string id = body["instance_id"].get<std::string>();

                health_monitor_->RemoveCheck(svc, id);
                bool ok = registry_->Deregister(svc, id);

                res->set(http::field::content_type, "application/json");
                res->result(ok ? http::status::ok : http::status::not_found);
                res->body() = ok ? "{\"status\":\"deregistered\"}" : "{\"error\":\"instance not found\"}";
            } catch (...) {
                res->result(http::status::bad_request);
                res->body() = "{\"error\":\"invalid json payload\"}";
            }
        } else if (target == "/api/v1/config" && req->method() == http::verb::get) {
            res->set(http::field::content_type, "application/json");
            json j = json::array();
            auto configs = config_mgr_->GetAll();
            for (const auto& c : configs) {
                j.push_back({
                    {"config_id", c.config_id()},
                    {"key", c.config_key()},
                    {"value", c.config_value()},
                    {"version", c.version()},
                    {"author", c.author()}
                });
            }
            res->body() = j.dump(2);
        } else if (target == "/api/v1/config" && req->method() == http::verb::post) {
            try {
                auto body = json::parse(req->body());
                std::string cid = body["config_id"].get<std::string>();
                std::string k = body["key"].get<std::string>();
                std::string v = body["value"].get<std::string>();
                std::string author = body.value("author", "admin");

                bool ok = config_mgr_->Set(cid, k, v, proto::SCOPE_CLUSTER, "", author);
                res->set(http::field::content_type, "application/json");
                res->result(ok ? http::status::ok : http::status::bad_request);
                res->body() = ok ? "{\"status\":\"updated\"}" : "{\"error\":\"config update failed\"}";
            } catch (...) {
                res->result(http::status::bad_request);
                res->body() = "{\"error\":\"invalid json payload\"}";
            }
        } else {
            res->result(http::status::not_found);
            res->set(http::field::content_type, "application/json");
            res->body() = "{\"error\":\"Not Found\"}";
        }

        res->prepare_payload();
        http::async_write(*stream, *res, [stream, res](auto, auto) {});
    });
}

std::string AgentHttpServer::GeneratePrometheusMetrics() const {
    std::ostringstream ss;
    size_t alive = membership_mgr_->AliveCount();
    size_t total_members = membership_mgr_->MemberCount();

    ss << "# HELP cluster_nodes Number of nodes in cluster by status\n";
    ss << "# TYPE cluster_nodes gauge\n";
    ss << "cluster_nodes{status=\"alive\"} " << alive << "\n";
    ss << "cluster_nodes{status=\"total\"} " << total_members << "\n";

    if (transport_) {
        ss << "# HELP gossip_messages_sent_total Total gossip messages sent\n";
        ss << "# TYPE gossip_messages_sent_total counter\n";
        ss << "gossip_messages_sent_total " << transport_->MessagesSent() << "\n";

        ss << "# HELP gossip_messages_received_total Total gossip messages received\n";
        ss << "# TYPE gossip_messages_received_total counter\n";
        ss << "gossip_messages_received_total " << transport_->MessagesReceived() << "\n";

        ss << "# HELP gossip_bytes_sent_total Total gossip bytes sent\n";
        ss << "# TYPE gossip_bytes_sent_total counter\n";
        ss << "gossip_bytes_sent_total " << transport_->BytesSent() << "\n";

        ss << "# HELP gossip_bytes_received_total Total gossip bytes received\n";
        ss << "# TYPE gossip_bytes_received_total counter\n";
        ss << "gossip_bytes_received_total " << transport_->BytesReceived() << "\n";
    }

    if (failure_detector_) {
        ss << "# HELP failure_probes_sent_total Total failure detection probes sent\n";
        ss << "# TYPE failure_probes_sent_total counter\n";
        ss << "failure_probes_sent_total " << failure_detector_->ProbesSent() << "\n";

        ss << "# HELP failure_direct_acks_total Total direct probe ACKs received\n";
        ss << "# TYPE failure_direct_acks_total counter\n";
        ss << "failure_direct_acks_total " << failure_detector_->DirectAcks() << "\n";

        ss << "# HELP failure_indirect_acks_total Total indirect probe ACKs received\n";
        ss << "# TYPE failure_indirect_acks_total counter\n";
        ss << "failure_indirect_acks_total " << failure_detector_->IndirectAcks() << "\n";

        ss << "# HELP failure_suspicions_total Total suspicions declared\n";
        ss << "# TYPE failure_suspicions_total counter\n";
        ss << "failure_suspicions_total " << failure_detector_->SuspicionsDeclared() << "\n";

        ss << "# HELP failure_deaths_total Total deaths declared\n";
        ss << "# TYPE failure_deaths_total counter\n";
        ss << "failure_deaths_total " << failure_detector_->DeathsDeclared() << "\n";
    }

    if (registry_) {
        ss << "# HELP service_instances Number of registered service instances\n";
        ss << "# TYPE service_instances gauge\n";
        ss << "service_instances{health=\"healthy\"} " << registry_->ActiveInstanceCount() << "\n";
    }

    if (health_monitor_) {
        ss << "# HELP service_health_checks_total Total active health checks executed\n";
        ss << "# TYPE service_health_checks_total counter\n";
        ss << "service_health_checks_total{result=\"passed\"} " << health_monitor_->TotalChecksPassed() << "\n";
        ss << "service_health_checks_total{result=\"failed\"} " << health_monitor_->TotalChecksFailed() << "\n";
    }

    return ss.str();
}

} // namespace dicp
