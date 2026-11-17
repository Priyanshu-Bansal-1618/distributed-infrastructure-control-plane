#include "dissemination_queue.hpp"

namespace dicp {

DisseminationQueue::DisseminationQueue(uint32_t max_pending, uint32_t retransmit_factor)
    : max_pending_(max_pending),
      retransmit_factor_(retransmit_factor) {}

void DisseminationQueue::Enqueue(const proto::NodeUpdate& update) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (node_queue_.size() >= max_pending_) {
        node_queue_.pop_front();
    }
    node_queue_.push_back({update, retransmit_factor_});
}

void DisseminationQueue::Enqueue(const proto::ServiceUpdate& update) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (service_queue_.size() >= max_pending_) {
        service_queue_.pop_front();
    }
    service_queue_.push_back({update, retransmit_factor_});
}

void DisseminationQueue::Enqueue(const proto::ConfigUpdate& update) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config_queue_.size() >= max_pending_) {
        config_queue_.pop_front();
    }
    config_queue_.push_back({update, retransmit_factor_});
}

proto::GossipBatch DisseminationQueue::GetBatch(size_t max_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    proto::GossipBatch batch;
    size_t current_size = 0;

    // Node updates
    size_t n_nodes = node_queue_.size();
    for (size_t i = 0; i < n_nodes; ++i) {
        auto item = node_queue_.front();
        node_queue_.pop_front();

        size_t item_size = item.update.ByteSizeLong();
        if (current_size + item_size > max_bytes) {
            node_queue_.push_back(item);
            break;
        }

        *batch.add_node_updates() = item.update;
        current_size += item_size;

        if (item.retransmit_count > 1) {
            item.retransmit_count--;
            node_queue_.push_back(item);
        }
    }

    // Service updates
    size_t n_services = service_queue_.size();
    for (size_t i = 0; i < n_services; ++i) {
        auto item = service_queue_.front();
        service_queue_.pop_front();

        size_t item_size = item.update.ByteSizeLong();
        if (current_size + item_size > max_bytes) {
            service_queue_.push_back(item);
            break;
        }

        *batch.add_service_updates() = item.update;
        current_size += item_size;

        if (item.retransmit_count > 1) {
            item.retransmit_count--;
            service_queue_.push_back(item);
        }
    }

    // Config updates
    size_t n_configs = config_queue_.size();
    for (size_t i = 0; i < n_configs; ++i) {
        auto item = config_queue_.front();
        config_queue_.pop_front();

        size_t item_size = item.update.ByteSizeLong();
        if (current_size + item_size > max_bytes) {
            config_queue_.push_back(item);
            break;
        }

        *batch.add_config_updates() = item.update;
        current_size += item_size;

        if (item.retransmit_count > 1) {
            item.retransmit_count--;
            config_queue_.push_back(item);
        }
    }

    return batch;
}

size_t DisseminationQueue::Size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return node_queue_.size() + service_queue_.size() + config_queue_.size();
}

void DisseminationQueue::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    node_queue_.clear();
    service_queue_.clear();
    config_queue_.clear();
}

} // namespace dicp
