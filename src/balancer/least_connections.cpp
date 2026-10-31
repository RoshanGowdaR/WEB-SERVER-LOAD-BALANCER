#include "balancer/least_connections.h"
#include <mutex>
#include <limits>

namespace balancer {

LeastConnectionsBalancer::LeastConnectionsBalancer(std::vector<std::shared_ptr<BackendServer>> backends)
    : backends_(std::move(backends)) {}

void LeastConnectionsBalancer::add_backend(std::shared_ptr<BackendServer> backend) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    backends_.push_back(std::move(backend));
}

void LeastConnectionsBalancer::set_backends(std::vector<std::shared_ptr<BackendServer>> backends) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    backends_ = std::move(backends);
}

std::vector<std::shared_ptr<BackendServer>> LeastConnectionsBalancer::get_backends() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return backends_;
}

std::shared_ptr<BackendServer> LeastConnectionsBalancer::select_backend(const http::HttpRequest& /*req*/) {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (backends_.empty()) {
        return nullptr;
    }

    std::shared_ptr<BackendServer> best_backend = nullptr;
    uint32_t min_connections = std::numeric_limits<uint32_t>::max();

    // Start index offset to tie-break uniformly among backends with equal load
    size_t count = backends_.size();
    size_t start_offset = tie_breaker_index_.fetch_add(1, std::memory_order_relaxed) % count;

    for (size_t i = 0; i < count; ++i) {
        size_t idx = (start_offset + i) % count;
        const auto& backend = backends_[idx];
        if (backend->is_healthy.load(std::memory_order_relaxed)) {
            uint32_t conns = backend->active_connections.load(std::memory_order_relaxed);
            if (conns < min_connections) {
                min_connections = conns;
                best_backend = backend;
            }
        }
    }

    return best_backend;
}

} // namespace balancer
