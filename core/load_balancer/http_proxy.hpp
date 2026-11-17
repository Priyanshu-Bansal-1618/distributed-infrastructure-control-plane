#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <memory>
#include <string>
#include <atomic>
#include <mutex>
#include "backend_snapshot.hpp"
#include "load_balancer_strategy.hpp"
#include "core/common/config.hpp"

namespace dicp {

class HttpProxy : public std::enable_shared_from_this<HttpProxy> {
public:
    HttpProxy(boost::asio::io_context& ioc, const LoadBalancerConfig& config);
    ~HttpProxy();

    void Start();
    void Stop();

    void UpdateSnapshot(std::shared_ptr<const BackendSnapshot> new_snapshot);
    std::shared_ptr<const BackendSnapshot> GetCurrentSnapshot() const;

    void SetStrategy(std::unique_ptr<LoadBalancerStrategy> strategy);
    std::string GetStrategyName() const;

    uint16_t LocalPort() const;

    uint64_t TotalRequests() const { return total_requests_.load(); }
    uint64_t SuccessfulRequests() const { return successful_requests_.load(); }
    uint64_t FailedRequests() const { return failed_requests_.load(); }

private:
    void StartAccept();
    void HandleSession(std::shared_ptr<boost::beast::tcp_stream> stream);
    void ForwardRequest(std::shared_ptr<boost::beast::tcp_stream> client_stream,
                        std::shared_ptr<boost::beast::http::request<boost::beast::http::string_body>> req,
                        std::shared_ptr<BackendEndpoint> backend);

    boost::asio::io_context& ioc_;
    LoadBalancerConfig config_;
    boost::asio::ip::tcp::acceptor acceptor_;

    std::shared_ptr<const BackendSnapshot> snapshot_;
    mutable std::mutex strategy_mutex_;
    std::unique_ptr<LoadBalancerStrategy> strategy_;

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> successful_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};
};

} // namespace dicp
