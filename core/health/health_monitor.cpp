#include "health_monitor.hpp"
#include "core/common/logger.hpp"
#include "core/common/types.hpp"

namespace dicp {

namespace http = boost::beast::http;

HealthMonitor::HealthMonitor(boost::asio::io_context& ioc,
                             const AgentConfig& config,
                             std::shared_ptr<ServiceRegistry> registry)
    : ioc_(ioc),
      config_(config),
      registry_(registry) {}

HealthMonitor::~HealthMonitor() {
    Stop();
}

void HealthMonitor::Start() {
    if (running_.exchange(true)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [key, state] : checks_) {
        ScheduleCheck(state);
    }
}

void HealthMonitor::Stop() {
    if (!running_.exchange(false)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    boost::system::error_code ec;
    for (auto& [key, state] : checks_) {
        if (state->check_timer) {
            state->check_timer->cancel(ec);
        }
    }
}

void HealthMonitor::AddCheck(const HealthCheckConfig& check_cfg) {
    std::string key = check_cfg.service_name + "/" + check_cfg.instance_id;
    auto state = std::make_shared<HealthCheckState>();
    state->config = check_cfg;
    state->current_health = proto::HEALTH_UNKNOWN;
    state->check_timer = std::make_shared<boost::asio::steady_timer>(ioc_);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        checks_[key] = state;
    }

    DICP_LOG_INFO("HealthMonitor", "Added active health check for " + key + 
                  " targeting http://" + check_cfg.host + ":" + std::to_string(check_cfg.port) + check_cfg.path);

    if (running_) {
        ScheduleCheck(state);
    }
}

void HealthMonitor::RemoveCheck(const std::string& service_name, const std::string& instance_id) {
    std::string key = service_name + "/" + instance_id;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = checks_.find(key);
    if (it != checks_.end()) {
        boost::system::error_code ec;
        if (it->second->check_timer) {
            it->second->check_timer->cancel(ec);
        }
        checks_.erase(it);
        DICP_LOG_INFO("HealthMonitor", "Removed health check for " + key);
    }
}

std::optional<HealthCheckState> HealthMonitor::GetState(const std::string& service_name, const std::string& instance_id) const {
    std::string key = service_name + "/" + instance_id;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = checks_.find(key);
    if (it != checks_.end()) {
        return *it->second;
    }
    return std::nullopt;
}

void HealthMonitor::ScheduleCheck(std::shared_ptr<HealthCheckState> state) {
    if (!running_) return;

    state->check_timer->expires_after(std::chrono::seconds(state->config.interval_seconds));
    auto self = shared_from_this();
    state->check_timer->async_wait([this, self, state](boost::system::error_code ec) {
        if (!ec && running_) {
            ExecuteCheck(state);
            ScheduleCheck(state);
        }
    });
}

void HealthMonitor::ExecuteCheck(std::shared_ptr<HealthCheckState> state) {
    checks_executed_++;
    uint64_t start_time = SteadyClockMs();

    // Spawn an async HTTP probe via Beast
    auto stream = std::make_shared<boost::beast::tcp_stream>(ioc_);
    stream->expires_after(std::chrono::milliseconds(state->config.timeout_ms));

    boost::asio::ip::tcp::resolver resolver(ioc_);
    boost::system::error_code ec;
    auto endpoints = resolver.resolve(state->config.host, std::to_string(state->config.port), ec);

    if (ec) {
        checks_failed_++;
        state->consecutive_failures++;
        state->consecutive_successes = 0;
        state->last_status_code = 0;
        state->last_check_time_sec = CurrentTimeSec();

        if (state->consecutive_failures >= state->config.failure_threshold &&
            state->current_health != proto::HEALTH_UNHEALTHY) {
            state->current_health = proto::HEALTH_UNHEALTHY;
            registry_->UpdateHealth(state->config.service_name, state->config.instance_id, proto::HEALTH_UNHEALTHY);
            DICP_LOG_WARN("HealthMonitor", "Service " + state->config.service_name + "/" + 
                          state->config.instance_id + " failed resolve: marked UNHEALTHY");
        }
        return;
    }

    auto req = std::make_shared<http::request<http::empty_body>>(http::verb::get, state->config.path, 11);
    req->set(http::field::host, state->config.host + ":" + std::to_string(state->config.port));
    req->set(http::field::user_agent, "DICP-HealthMonitor/1.0");

    auto res = std::make_shared<http::response<http::string_body>>();
    auto buffer = std::make_shared<boost::beast::flat_buffer>();

    stream->async_connect(endpoints, [this, state, stream, req, res, buffer, start_time](boost::beast::error_code ec, auto) {
        if (ec) {
            checks_failed_++;
            state->consecutive_failures++;
            state->consecutive_successes = 0;
            state->last_status_code = 0;
            state->last_latency_ms = SteadyClockMs() - start_time;
            state->last_check_time_sec = CurrentTimeSec();

            if (state->consecutive_failures >= state->config.failure_threshold &&
                state->current_health != proto::HEALTH_UNHEALTHY) {
                state->current_health = proto::HEALTH_UNHEALTHY;
                registry_->UpdateHealth(state->config.service_name, state->config.instance_id, proto::HEALTH_UNHEALTHY);
                DICP_LOG_WARN("HealthMonitor", "Service " + state->config.service_name + "/" + 
                              state->config.instance_id + " connect failed (" + ec.message() + "): marked UNHEALTHY");
            }
            return;
        }

        http::async_write(*stream, *req, [this, state, stream, res, buffer, start_time](boost::beast::error_code ec, auto) {
            if (ec) {
                checks_failed_++;
                state->consecutive_failures++;
                state->consecutive_successes = 0;
                return;
            }

            http::async_read(*stream, *buffer, *res, [this, state, stream, res, start_time](boost::beast::error_code ec, auto) {
                state->last_latency_ms = SteadyClockMs() - start_time;
                state->last_check_time_sec = CurrentTimeSec();

                // Gracefully shut down stream
                boost::beast::error_code close_ec;
                stream->socket().shutdown(boost::asio::ip::tcp::socket::shutdown_both, close_ec);

                if (!ec && res->result() == http::status::ok) {
                    checks_passed_++;
                    state->consecutive_successes++;
                    state->consecutive_failures = 0;
                    state->last_status_code = static_cast<uint32_t>(res->result_int());

                    if (state->consecutive_successes >= state->config.recovery_threshold &&
                        state->current_health != proto::HEALTH_HEALTHY) {
                        state->current_health = proto::HEALTH_HEALTHY;
                        registry_->UpdateHealth(state->config.service_name, state->config.instance_id, proto::HEALTH_HEALTHY);
                        DICP_LOG_INFO("HealthMonitor", "Service " + state->config.service_name + "/" + 
                                      state->config.instance_id + " recovered: marked HEALTHY");
                    }
                } else {
                    checks_failed_++;
                    state->consecutive_failures++;
                    state->consecutive_successes = 0;
                    state->last_status_code = ec ? 0 : static_cast<uint32_t>(res->result_int());

                    if (state->consecutive_failures >= state->config.failure_threshold &&
                        state->current_health != proto::HEALTH_UNHEALTHY) {
                        state->current_health = proto::HEALTH_UNHEALTHY;
                        registry_->UpdateHealth(state->config.service_name, state->config.instance_id, proto::HEALTH_UNHEALTHY);
                        DICP_LOG_WARN("HealthMonitor", "Service " + state->config.service_name + "/" + 
                                      state->config.instance_id + " check failed (status=" + 
                                      std::to_string(state->last_status_code) + "): marked UNHEALTHY");
                    }
                }
            });
        });
    });
}

} // namespace dicp
