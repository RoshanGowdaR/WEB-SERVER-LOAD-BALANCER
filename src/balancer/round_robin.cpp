#include "balancer/round_robin.h"
#include <mutex>

namespace balancer {

RoundRobinBalancer::RoundRobinBalancer(std::vector<std::shared_ptr<BackendServer>> backends)
    : backends_(std::move(backends)) {}

void RoundRobinBalancer::add_backend(std::shared_ptr<BackendServer> backend) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    backends_.push_back(std::move(backend));
}

void RoundRobinBalancer::set_backends(std::vector<std::shared_ptr<BackendServer>> backends) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    backends_ = std::move(backends);
    index_.store(0);
}

std::vector<std::shared_ptr<BackendServer>> RoundRobinBalancer::get_backends() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return backends_;
}

std::shared_ptr<BackendServer> RoundRobinBalancer::select_backend(const http::HttpRequest& /*req*/) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (backends_.empty()) {
        return nullptr;
    }

    size_t count = backends_.size();
    // Try round-robin search across all nodes to find the next healthy backend
    for (size_t i = 0; i < count; ++i) {
        size_t idx = index_.fetch_add(1, std::memory_order_relaxed) % count;
        if (backends_[idx]->is_healthy.load(std::memory_order_relaxed)) {
            return backends_[idx];
        }
    }

    // All backends are marked unhealthy: return nullptr so caller can respond with 503 Service Unavailable
    return nullptr;
}

} // namespace balancer
