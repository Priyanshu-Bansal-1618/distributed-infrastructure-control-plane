#include <gtest/gtest.h>
#include "core/load_balancer/load_balancer_strategy.hpp"
#include "core/load_balancer/http_proxy.hpp"
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <thread>
#include <chrono>

using namespace dicp;
namespace http = boost::beast::http;

TEST(LoadBalancerTest, RoundRobinStrategy) {
    RoundRobinStrategy rr;
    std::vector<std::shared_ptr<BackendEndpoint>> backends = {
        std::make_shared<BackendEndpoint>("b1", "svc", "127.0.0.1", 8001),
        std::make_shared<BackendEndpoint>("b2", "svc", "127.0.0.1", 8002),
        std::make_shared<BackendEndpoint>("b3", "svc", "127.0.0.1", 8003)
    };

    EXPECT_EQ(rr.Select(backends)->instance_id, "b1");
    EXPECT_EQ(rr.Select(backends)->instance_id, "b2");
    EXPECT_EQ(rr.Select(backends)->instance_id, "b3");
    EXPECT_EQ(rr.Select(backends)->instance_id, "b1");
}

TEST(LoadBalancerTest, WeightedRoundRobinStrategy) {
    WeightedRoundRobinStrategy wrr;
    std::vector<std::shared_ptr<BackendEndpoint>> backends = {
        std::make_shared<BackendEndpoint>("heavy", "svc", "127.0.0.1", 8001, 3), // weight 3
        std::make_shared<BackendEndpoint>("light", "svc", "127.0.0.1", 8002, 1)  // weight 1
    };

    std::unordered_map<std::string, int> counts;
    for (int i = 0; i < 400; ++i) {
        auto b = wrr.Select(backends);
        counts[b->instance_id]++;
    }

    // Heavy (weight 3) should receive ~300 requests, Light (weight 1) should receive ~100 requests
    EXPECT_EQ(counts["heavy"], 300);
    EXPECT_EQ(counts["light"], 100);
}

TEST(LoadBalancerTest, LeastConnectionsStrategy) {
    LeastConnectionsStrategy lc;
    auto b1 = std::make_shared<BackendEndpoint>("b1", "svc", "127.0.0.1", 8001);
    auto b2 = std::make_shared<BackendEndpoint>("b2", "svc", "127.0.0.1", 8002);
    auto b3 = std::make_shared<BackendEndpoint>("b3", "svc", "127.0.0.1", 8003);

    b1->active_connections = 10;
    b2->active_connections = 2; // lowest!
    b3->active_connections = 5;

    std::vector<std::shared_ptr<BackendEndpoint>> backends = {b1, b2, b3};
    EXPECT_EQ(lc.Select(backends)->instance_id, "b2");

    b2->active_connections = 15;
    EXPECT_EQ(lc.Select(backends)->instance_id, "b3"); // Now b3 is lowest (5)
}

TEST(LoadBalancerTest, ConsistentHashStrategy) {
    ConsistentHashStrategy ch;
    std::vector<std::shared_ptr<BackendEndpoint>> backends = {
        std::make_shared<BackendEndpoint>("node-A", "svc", "127.0.0.1", 8001),
        std::make_shared<BackendEndpoint>("node-B", "svc", "127.0.0.1", 8002),
        std::make_shared<BackendEndpoint>("node-C", "svc", "127.0.0.1", 8003)
    };

    // Consistent hashing must deterministically route same key to same backend
    auto b_user1 = ch.Select(backends, "customer-12345");
    auto b_user1_again = ch.Select(backends, "customer-12345");
    EXPECT_EQ(b_user1->instance_id, b_user1_again->instance_id);

    auto b_user2 = ch.Select(backends, "customer-98765");
    auto b_user2_again = ch.Select(backends, "customer-98765");
    EXPECT_EQ(b_user2->instance_id, b_user2_again->instance_id);
}

TEST(LoadBalancerTest, HttpProxyEndToEndRouting) {
    boost::asio::io_context ioc;
    auto work = boost::asio::make_work_guard(ioc);

    // 1. Create a dummy HTTP backend server on port 28999
    boost::asio::ip::tcp::acceptor backend_acceptor(ioc);
    boost::asio::ip::tcp::endpoint backend_ep(boost::asio::ip::make_address("127.0.0.1"), 28999);
    backend_acceptor.open(backend_ep.protocol());
    backend_acceptor.set_option(boost::asio::socket_base::reuse_address(true));
    backend_acceptor.bind(backend_ep);
    backend_acceptor.listen();

    std::atomic<bool> backend_hit{false};

    std::function<void()> accept_backend = [&]() {
        backend_acceptor.async_accept([&backend_acceptor, &backend_hit](boost::system::error_code ec, boost::asio::ip::tcp::socket sock) {
            if (!ec) {
                auto stream = std::make_shared<boost::beast::tcp_stream>(std::move(sock));
                auto buffer = std::make_shared<boost::beast::flat_buffer>();
                auto req = std::make_shared<http::request<http::string_body>>();
                http::async_read(*stream, *buffer, *req, [&backend_hit, stream, buffer, req](auto, auto) {
                    backend_hit = true;
                    auto res = std::make_shared<http::response<http::string_body>>(http::status::ok, req->version());
                    res->set(http::field::content_type, "application/json");
                    res->body() = "{\"status\":\"backend_ok\"}";
                    res->prepare_payload();
                    http::async_write(*stream, *res, [stream, res](auto, auto) {});
                });
            }
        });
    };
    accept_backend();

    // 2. Start HttpProxy on port 28081
    LoadBalancerConfig lb_cfg;
    lb_cfg.listen_address = "127.0.0.1";
    lb_cfg.listen_port = 28081;
    lb_cfg.strategy = "round_robin";

    auto proxy = std::make_shared<HttpProxy>(ioc, lb_cfg);
    proxy->Start();

    // 3. Publish snapshot with the dummy backend
    auto snap = std::make_shared<BackendSnapshot>(1, 1000);
    snap->AddEndpoint(std::make_shared<BackendEndpoint>("backend-1", "order-service", "127.0.0.1", 28999));
    proxy->UpdateSnapshot(snap);

    std::thread io_thread([&ioc]() {
        ioc.run();
    });

    // 4. Send client HTTP request to proxy
    boost::asio::io_context client_ioc;
    boost::beast::tcp_stream client_stream(client_ioc);
    boost::asio::ip::tcp::endpoint proxy_ep(boost::asio::ip::make_address("127.0.0.1"), 28081);
    client_stream.connect(proxy_ep);

    http::request<http::string_body> client_req{http::verb::get, "/order/status", 11};
    client_req.set(http::field::host, "127.0.0.1:28081");
    client_req.set("X-Service-Name", "order-service");
    http::write(client_stream, client_req);

    boost::beast::flat_buffer client_buf;
    http::response<http::string_body> client_res;
    http::read(client_stream, client_buf, client_res);

    EXPECT_EQ(client_res.result(), http::status::ok);
    EXPECT_TRUE(client_res.body().find("backend_ok") != std::string::npos);
    EXPECT_TRUE(backend_hit.load());

    // Clean shutdown
    proxy->Stop();
    boost::system::error_code ec;
    backend_acceptor.close(ec);
    work.reset();
    ioc.stop();

    if (io_thread.joinable()) {
        io_thread.join();
    }
}
