#include "http_proxy.hpp"
#include "core/common/logger.hpp"
#include <nlohmann/json.hpp>

namespace dicp {

namespace http = boost::beast::http;

HttpProxy::HttpProxy(boost::asio::io_context& ioc, const LoadBalancerConfig& config)
    : ioc_(ioc),
      config_(config),
      acceptor_(ioc) {
    boost::asio::ip::tcp::endpoint ep(
        boost::asio::ip::make_address(config_.listen_address),
        config_.listen_port);

    acceptor_.open(ep.protocol());
    acceptor_.set_option(boost::asio::socket_base::reuse_address(true));
    acceptor_.bind(ep);
    acceptor_.listen();

    // Default strategy
    if (config_.strategy == "weighted_round_robin") {
        strategy_ = std::make_unique<WeightedRoundRobinStrategy>();
    } else if (config_.strategy == "least_conn" || config_.strategy == "least_connections") {
        strategy_ = std::make_unique<LeastConnectionsStrategy>();
    } else if (config_.strategy == "consistent_hash") {
        strategy_ = std::make_unique<ConsistentHashStrategy>();
    } else {
        strategy_ = std::make_unique<RoundRobinStrategy>();
    }

    // Initial empty snapshot
    snapshot_ = std::make_shared<const BackendSnapshot>(1, 0);
}

HttpProxy::~HttpProxy() {
    Stop();
}

void HttpProxy::Start() {
    if (running_.exchange(true)) return;
    DICP_LOG_INFO("HttpProxy", "Starting HTTP Reverse Proxy on port " + std::to_string(LocalPort()) +
                  " (strategy=" + GetStrategyName() + ")");
    StartAccept();
}

void HttpProxy::Stop() {
    if (!running_.exchange(false)) return;
    boost::system::error_code ec;
    acceptor_.close(ec);
    DICP_LOG_INFO("HttpProxy", "HTTP Reverse Proxy stopped");
}

uint16_t HttpProxy::LocalPort() const {
    boost::system::error_code ec;
    return acceptor_.local_endpoint(ec).port();
}

void HttpProxy::UpdateSnapshot(std::shared_ptr<const BackendSnapshot> new_snapshot) {
    std::atomic_store(&snapshot_, new_snapshot);
    DICP_LOG_INFO("HttpProxy", "Updated backend snapshot to version=" + 
                  std::to_string(new_snapshot->Version()));
}

std::shared_ptr<const BackendSnapshot> HttpProxy::GetCurrentSnapshot() const {
    return std::atomic_load(&snapshot_);
}

void HttpProxy::SetStrategy(std::unique_ptr<LoadBalancerStrategy> strategy) {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    strategy_ = std::move(strategy);
}

std::string HttpProxy::GetStrategyName() const {
    std::lock_guard<std::mutex> lock(strategy_mutex_);
    return strategy_ ? strategy_->Name() : "none";
}

void HttpProxy::StartAccept() {
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

void HttpProxy::HandleSession(std::shared_ptr<boost::beast::tcp_stream> stream) {
    auto buffer = std::make_shared<boost::beast::flat_buffer>();
    auto req = std::make_shared<http::request<http::string_body>>();

    stream->expires_after(std::chrono::milliseconds(config_.connection_timeout_ms));
    http::async_read(*stream, *buffer, *req, [this, stream, buffer, req](boost::beast::error_code ec, auto) {
        if (ec) return;

        total_requests_++;
        std::string target = std::string(req->target());

        // Internal Admin endpoints
        if (target == "/admin/backends") {
            auto snap = GetCurrentSnapshot();
            nlohmann::json j;
            j["version"] = snap->Version();
            j["strategy"] = GetStrategyName();
            for (const auto& [svc, endpoints] : snap->AllBackends()) {
                for (const auto& ep : endpoints) {
                    j["backends"][svc].push_back({
                        {"instance_id", ep->instance_id},
                        {"host", ep->host},
                        {"port", ep->port},
                        {"weight", ep->weight},
                        {"active_connections", ep->active_connections.load()},
                        {"total_requests", ep->total_requests.load()}
                    });
                }
            }
            auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req->version());
            res->set(http::field::content_type, "application/json");
            res->body() = j.dump(2);
            res->prepare_payload();
            http::async_write(*stream, *res, [stream, res](auto, auto) {});
            successful_requests_++;
            return;
        }

        if (target == "/admin/snapshot" && req->method() == http::verb::post) {
            try {
                auto body_json = nlohmann::json::parse(req->body());
                auto snap = GetCurrentSnapshot();
                uint64_t ver = body_json.value("version", snap ? (snap->Version() + 1) : 1);
                auto new_snap = std::make_shared<BackendSnapshot>(ver, 0);
                if (body_json.contains("backends") && body_json["backends"].is_object()) {
                    for (auto& [svc, list] : body_json["backends"].items()) {
                        for (auto& item : list) {
                            std::string inst_id = item.value("instance_id", "");
                            std::string host = item.value("host", "127.0.0.1");
                            uint16_t port = static_cast<uint16_t>(item.value("port", 80));
                            uint32_t weight = static_cast<uint32_t>(item.value("weight", 1));
                            auto ep = std::make_shared<BackendEndpoint>(inst_id, svc, host, port, weight);
                            new_snap->AddEndpoint(ep);
                        }
                    }
                }
                UpdateSnapshot(new_snap);
                auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req->version());
                res->set(http::field::content_type, "application/json");
                res->body() = "{\"status\":\"success\",\"version\":" + std::to_string(ver) + "}";
                res->prepare_payload();
                http::async_write(*stream, *res, [stream, res](auto, auto) {});
                successful_requests_++;
                return;
            } catch (const std::exception& e) {
                auto res = std::make_shared<http::response<http::string_body>>(http::status::bad_request, req->version());
                res->body() = std::string("{\"error\":\"") + e.what() + "\"}";
                res->prepare_payload();
                http::async_write(*stream, *res, [stream, res](auto, auto) {});
                failed_requests_++;
                return;
            }
        }

        if (target == "/admin/strategy" && req->method() == http::verb::post) {
            try {
                auto body_json = nlohmann::json::parse(req->body());
                std::string strat = body_json.value("strategy", "round_robin");
                if (strat == "weighted_round_robin") {
                    SetStrategy(std::make_unique<WeightedRoundRobinStrategy>());
                } else if (strat == "least_conn" || strat == "least_connections") {
                    SetStrategy(std::make_unique<LeastConnectionsStrategy>());
                } else if (strat == "consistent_hash") {
                    SetStrategy(std::make_unique<ConsistentHashStrategy>());
                } else {
                    SetStrategy(std::make_unique<RoundRobinStrategy>());
                }
                auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req->version());
                res->set(http::field::content_type, "application/json");
                res->body() = "{\"status\":\"success\",\"strategy\":\"" + GetStrategyName() + "\"}";
                res->prepare_payload();
                http::async_write(*stream, *res, [stream, res](auto, auto) {});
                successful_requests_++;
                return;
            } catch (const std::exception& e) {
                auto res = std::make_shared<http::response<http::string_body>>(http::status::bad_request, req->version());
                res->body() = std::string("{\"error\":\"") + e.what() + "\"}";
                res->prepare_payload();
                http::async_write(*stream, *res, [stream, res](auto, auto) {});
                failed_requests_++;
                return;
            }
        }

        if (target == "/metrics") {
            std::string prom;
            prom += "# HELP dicp_proxy_requests_total Total requests processed by proxy\n";
            prom += "# TYPE dicp_proxy_requests_total counter\n";
            prom += "dicp_proxy_requests_total " + std::to_string(total_requests_.load()) + "\n";
            prom += "# HELP dicp_proxy_requests_successful_total Successful requests\n";
            prom += "# TYPE dicp_proxy_requests_successful_total counter\n";
            prom += "dicp_proxy_requests_successful_total " + std::to_string(successful_requests_.load()) + "\n";
            prom += "# HELP dicp_proxy_requests_failed_total Failed requests\n";
            prom += "# TYPE dicp_proxy_requests_failed_total counter\n";
            prom += "dicp_proxy_requests_failed_total " + std::to_string(failed_requests_.load()) + "\n";

            auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req->version());
            res->set(http::field::content_type, "text/plain; version=0.0.4");
            res->body() = prom;
            res->prepare_payload();
            http::async_write(*stream, *res, [stream, res](auto, auto) {});
            successful_requests_++;
            return;
        }

        // Determine service name
        std::string service_name;
        auto svc_hdr = req->find("X-Service-Name");
        if (svc_hdr != req->end()) {
            service_name = std::string(svc_hdr->value());
        } else if (target.rfind("/service/", 0) == 0) {
            // Path prefix format: /service/<name>/...
            size_t next_slash = target.find('/', 9);
            if (next_slash != std::string::npos) {
                service_name = target.substr(9, next_slash - 9);
            } else {
                service_name = target.substr(9);
            }
        } else {
            // Default service
            service_name = "default";
        }

        // Routing key for consistent hashing
        std::string routing_key = target;
        auto key_hdr = req->find("X-Routing-Key");
        if (key_hdr != req->end()) {
            routing_key = std::string(key_hdr->value());
        }

        // Read immutable snapshot without holding locks
        auto snap = GetCurrentSnapshot();
        auto endpoints = snap->GetEndpoints(service_name);

        if (endpoints.empty()) {
            failed_requests_++;
            auto res = std::make_shared<http::response<http::string_body>>(http::status::service_unavailable, req->version());
            res->set(http::field::content_type, "application/json");
            res->body() = "{\"error\":\"Service Unavailable\",\"service\":\"" + service_name + 
                          "\",\"message\":\"No eligible healthy backends\"}";
            res->prepare_payload();
            http::async_write(*stream, *res, [stream, res](auto, auto) {});
            return;
        }

        std::shared_ptr<BackendEndpoint> chosen;
        {
            std::lock_guard<std::mutex> lock(strategy_mutex_);
            chosen = strategy_->Select(endpoints, routing_key);
        }

        if (!chosen) {
            failed_requests_++;
            auto res = std::make_shared<http::response<http::string_body>>(http::status::service_unavailable, req->version());
            res->body() = "{\"error\":\"No backend selected\"}";
            res->prepare_payload();
            http::async_write(*stream, *res, [stream, res](auto, auto) {});
            return;
        }

        ForwardRequest(stream, req, chosen);
    });
}

void HttpProxy::ForwardRequest(std::shared_ptr<boost::beast::tcp_stream> client_stream,
                               std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> req,
                               std::shared_ptr<BackendEndpoint> backend) {
    backend->active_connections++;
    backend->total_requests++;

    auto upstream_stream = std::make_shared<boost::beast::tcp_stream>(ioc_);
    upstream_stream->expires_after(std::chrono::milliseconds(config_.connection_timeout_ms));

    boost::asio::ip::tcp::resolver resolver(ioc_);
    boost::system::error_code ec;
    auto endpoints = resolver.resolve(backend->host, std::to_string(backend->port), ec);

    if (ec) {
        backend->active_connections--;
        failed_requests_++;
        auto res = std::make_shared<http::response<http::string_body>>(http::status::bad_gateway, req->version());
        res->body() = "{\"error\":\"Bad Gateway\",\"message\":\"Failed to resolve upstream\"}";
        res->prepare_payload();
        http::async_write(*client_stream, *res, [client_stream, res](auto, auto) {});
        return;
    }

    // Adjust request headers for forwarding
    req->set(http::field::host, backend->EndpointString());

    upstream_stream->async_connect(endpoints, [this, client_stream, upstream_stream, req, backend](boost::beast::error_code ec, auto) {
        if (ec) {
            backend->active_connections--;
            failed_requests_++;
            auto res = std::make_shared<http::response<http::string_body>>(http::status::bad_gateway, req->version());
            res->body() = "{\"error\":\"Bad Gateway\",\"message\":\"Upstream connection refused\"}";
            res->prepare_payload();
            http::async_write(*client_stream, *res, [client_stream, res](auto, auto) {});
            return;
        }

        http::async_write(*upstream_stream, *req, [this, client_stream, upstream_stream, req, backend](boost::beast::error_code ec, auto) {
            if (ec) {
                backend->active_connections--;
                failed_requests_++;
                return;
            }

            auto buffer = std::make_shared<boost::beast::flat_buffer>();
            auto res = std::make_shared<http::response<http::string_body>>();

            http::async_read(*upstream_stream, *buffer, *res, [this, client_stream, upstream_stream, buffer, res, backend](boost::beast::error_code ec, auto) {
                backend->active_connections--;

                boost::beast::error_code close_ec;
                upstream_stream->socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, close_ec);

                if (ec) {
                    failed_requests_++;
                    auto err_res = std::make_shared<http::response<http::string_body>>(http::status::bad_gateway, 11);
                    err_res->body() = "{\"error\":\"Bad Gateway\",\"message\":\"Failed to read upstream response\"}";
                    err_res->prepare_payload();
                    http::async_write(*client_stream, *err_res, [client_stream, err_res](auto, auto) {});
                    return;
                }

                successful_requests_++;
                http::async_write(*client_stream, *res, [client_stream, res](auto, auto) {});
            });
        });
    });
}

} // namespace dicp
