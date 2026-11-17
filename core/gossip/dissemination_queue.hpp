#pragma once

#include <vector>
#include <deque>
#include <mutex>
#include <cstdint>
#include "proto/gossip.pb.h"
#include "proto/membership.pb.h"
#include "proto/registry.pb.h"
#include "proto/configuration.pb.h"

namespace dicp {

class DisseminationQueue {
public:
    explicit DisseminationQueue(uint32_t max_pending = 1000, uint32_t retransmit_factor = 3);

    void Enqueue(const proto::NodeUpdate& update);
    void Enqueue(const proto::ServiceUpdate& update);
    void Enqueue(const proto::ConfigUpdate& update);

    proto::GossipBatch GetBatch(size_t max_bytes);
    size_t Size() const;
    void Clear();

private:
    struct QueuedNodeUpdate {
        proto::NodeUpdate update;
        uint32_t retransmit_count;
    };

    struct QueuedServiceUpdate {
        proto::ServiceUpdate update;
        uint32_t retransmit_count;
    };

    struct QueuedConfigUpdate {
        proto::ConfigUpdate update;
        uint32_t retransmit_count;
    };

    uint32_t max_pending_;
    uint32_t retransmit_factor_;
    mutable std::mutex mutex_;

    std::deque<QueuedNodeUpdate> node_queue_;
    std::deque<QueuedServiceUpdate> service_queue_;
    std::deque<QueuedConfigUpdate> config_queue_;
};

} // namespace dicp
