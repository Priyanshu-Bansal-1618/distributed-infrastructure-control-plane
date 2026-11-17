#pragma once

#include "backend_snapshot.hpp"
#include <vector>
#include <memory>
#include <atomic>
#include <map>
#include <functional>
#include <algorithm>
#include <string>

namespace dicp {

class LoadBalancerStrategy {
public:
    virtual ~LoadBalancerStrategy() = default;
    virtual std::shared_ptr<BackendEndpoint> Select(
        const std::vector<std::shared_ptr<BackendEndpoint>>& backends,
        const std::string& routing_key = "") = 0;
    virtual std::string Name() const = 0;
};

// 1. Round Robin
class RoundRobinStrategy : public LoadBalancerStrategy {
public:
    std::shared_ptr<BackendEndpoint> Select(
        const std::vector<std::shared_ptr<BackendEndpoint>>& backends,
        const std::string& routing_key = "") override {
        (void)routing_key;
        if (backends.empty()) return nullptr;
        uint64_t idx = counter_.fetch_add(1);
        return backends[idx % backends.size()];
    }

    std::string Name() const override { return "round_robin"; }

private:
    std::atomic<uint64_t> counter_{0};
};

// 2. Weighted Round Robin
class WeightedRoundRobinStrategy : public LoadBalancerStrategy {
public:
    std::shared_ptr<BackendEndpoint> Select(
        const std::vector<std::shared_ptr<BackendEndpoint>>& backends,
        const std::string& routing_key = "") override {
        (void)routing_key;
        if (backends.empty()) return nullptr;
        if (backends.size() == 1) return backends[0];

        uint32_t total_weight = 0;
        for (const auto& b : backends) {
            total_weight += std::max(1U, b->weight);
        }

        uint64_t current = counter_.fetch_add(1) % total_weight;
        uint32_t cumulative = 0;
        for (const auto& b : backends) {
            cumulative += std::max(1U, b->weight);
            if (current < cumulative) {
                return b;
            }
        }
        return backends.front();
    }

    std::string Name() const override { return "weighted_round_robin"; }

private:
    std::atomic<uint64_t> counter_{0};
};

// 3. Least Connections
class LeastConnectionsStrategy : public LoadBalancerStrategy {
public:
    std::shared_ptr<BackendEndpoint> Select(
        const std::vector<std::shared_ptr<BackendEndpoint>>& backends,
        const std::string& routing_key = "") override {
        (void)routing_key;
        if (backends.empty()) return nullptr;

        std::shared_ptr<BackendEndpoint> best = backends[0];
        uint32_t min_conn = best->active_connections.load();

        for (size_t i = 1; i < backends.size(); ++i) {
            uint32_t conn = backends[i]->active_connections.load();
            if (conn < min_conn) {
                min_conn = conn;
                best = backends[i];
            }
        }
        return best;
    }

    std::string Name() const override { return "least_connections"; }
};

// 4. Consistent Hashing with virtual nodes
class ConsistentHashStrategy : public LoadBalancerStrategy {
public:
    explicit ConsistentHashStrategy(size_t virtual_nodes_per_backend = 100)
        : virtual_nodes_(virtual_nodes_per_backend) {}

    std::shared_ptr<BackendEndpoint> Select(
        const std::vector<std::shared_ptr<BackendEndpoint>>& backends,
        const std::string& routing_key = "") override {
        if (backends.empty()) return nullptr;

        // Build ring from current backends
        std::map<uint64_t, std::shared_ptr<BackendEndpoint>> ring;
        for (const auto& b : backends) {
            for (size_t v = 0; v < virtual_nodes_; ++v) {
                std::string vnode_key = b->instance_id + "#vnode" + std::to_string(v);
                uint64_t hash = HashKey(vnode_key);
                ring[hash] = b;
            }
        }

        if (ring.empty()) return backends[0];

        std::string key = routing_key.empty() ? "default" : routing_key;
        uint64_t key_hash = HashKey(key);

        auto it = ring.lower_bound(key_hash);
        if (it == ring.end()) {
            it = ring.begin(); // Wrap around ring
        }

        return it->second;
    }

    std::string Name() const override { return "consistent_hash"; }

private:
    static uint64_t HashKey(const std::string& s) {
        // FNV-1a 64-bit hash
        uint64_t hash = 14695981039346656037ULL;
        for (char c : s) {
            hash ^= static_cast<uint8_t>(c);
            hash *= 1099511628211ULL;
        }
        return hash;
    }

    size_t virtual_nodes_;
};

} // namespace dicp
