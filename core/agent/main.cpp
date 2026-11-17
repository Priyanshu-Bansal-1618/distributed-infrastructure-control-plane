#include "node_agent.hpp"
#include <iostream>
#include <csignal>

static std::atomic<bool> g_stop_requested{false};

void SignalHandler(int signum) {
    (void)signum;
    g_stop_requested.store(true);
}

int main(int argc, char* argv[]) {
    std::string config_path = "config.json";
    if (argc > 1) {
        config_path = argv[1];
    }

    dicp::AgentConfig config;
    try {
        config = dicp::AgentConfig::LoadFromFile(config_path);
    } catch (const std::exception& ex) {
        std::cerr << "Configuration file not found or invalid (" << config_path 
                  << "): " << ex.what() << ". Using defaults." << std::endl;
    }

    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);

    auto agent = std::make_unique<dicp::NodeAgent>(config);
    agent->Start();

    while (!g_stop_requested.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    agent->Stop();
    return 0;
}
