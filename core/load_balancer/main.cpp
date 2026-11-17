#include "http_proxy.hpp"
#include <iostream>
#include <csignal>
#include <thread>

static std::atomic<bool> g_stop_requested{false};

void SignalHandler(int signum) {
    (void)signum;
    g_stop_requested.store(true);
}

int main(int argc, char* argv[]) {
    dicp::LoadBalancerConfig config;
    if (argc > 1) {
        config.listen_port = static_cast<uint16_t>(std::stoi(argv[1]));
    }
    if (argc > 2) {
        config.strategy = argv[2];
    }

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    boost::asio::io_context ioc;
    auto proxy = std::make_shared<dicp::HttpProxy>(ioc, config);
    proxy->Start();

    std::vector<std::thread> workers;
    for (int i = 0; i < 2; ++i) {
        workers.emplace_back([&ioc]() {
            ioc.run();
        });
    }

    std::cout << "DICP HTTP Load Balancer listening on port " << proxy->LocalPort() << std::endl;

    while (!g_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    proxy->Stop();
    ioc.stop();

    for (auto& t : workers) {
        if (t.joinable()) t.join();
    }

    return 0;
}
