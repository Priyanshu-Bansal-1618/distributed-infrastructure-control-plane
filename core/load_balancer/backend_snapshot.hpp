#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <atomic>
#include <cstdint>

namespace dicp {

struct BackendEndpoint {
    std::string instance_id;
    std::string service_name;
    std::string host;
    uint16_t port;
    uint32_t weight{1};
    mutable std::atomic<uint32_t> active_connections{0};
    mutable std::atomic<uint64_t> total_requests{0};

    BackendEndpoint(std::string id, std::string svc, std::string h, uint16_t p, uint32_t w = 1)
        : instance_id(std::move(id)),
          service_name(std::move(svc)),
          host(std::move(h)),
          port(p),
          weight(w) {}

    std::string EndpointString() const {
        return host + ":" + std::to_string(port);
    }
};

class BackendSnapshot {
public:
    BackendSnapshot(uint64_t version, uint64_t timestamp_ms)
        : version_(version),
          timestamp_ms_(timestamp_ms) {}

    void AddEndpoint(std::shared_ptr<BackendEndpoint> endpoint) {
        backends_[endpoint->service_name].push_back(endpoint);
    }

    std::vector<std::shared_ptr<BackendEndpoint>> GetEndpoints(const std::string& service_name) const {
        auto it = backends_.find(service_name);
        if (it != backends_.end()) {
            return it->second;
        }
        return {};
    }

    uint64_t Version() const { return version_; }
    uint64_t TimestampMs() const { return timestamp_ms_; }

    const std::unordered_map<std::string, std::vector<std::shared_ptr<BackendEndpoint>>>& AllBackends() const {
        return backends_;
    }

private:
    uint64_t version_;
    uint64_t timestamp_ms_;
    std::unordered_map<std::string, std::vector<std::shared_ptr<BackendEndpoint>>> backends_;
};

} // namespace dicp
