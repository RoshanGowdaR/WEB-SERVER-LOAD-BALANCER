#pragma once

#include "http/request.h"
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>

namespace balancer {

struct BackendServer {
    std::string host;
    uint16_t port{0};
    int weight{1};

    std::atomic<bool> is_healthy{true};
    std::atomic<uint32_t> active_connections{0};
    std::atomic<uint64_t> total_requests{0};
    std::atomic<uint64_t> total_failures{0};
    std::atomic<uint32_t> consecutive_failures{0};
    std::atomic<uint32_t> consecutive_successes{0};

    [[nodiscard]] std::string address() const {
        return host + ":" + std::to_string(port);
    }
};

class ILoadBalancer {
public:
    virtual ~ILoadBalancer() = default;

    virtual void add_backend(std::shared_ptr<BackendServer> backend) = 0;
    virtual void set_backends(std::vector<std::shared_ptr<BackendServer>> backends) = 0;
    [[nodiscard]] virtual std::shared_ptr<BackendServer> select_backend(const http::HttpRequest& req) = 0;
    [[nodiscard]] virtual std::vector<std::shared_ptr<BackendServer>> get_backends() const = 0;
    [[nodiscard]] virtual std::string get_name() const = 0;
};

// RAII helper to increment and decrement active connections on a backend with underflow protection
class BackendConnectionGuard {
public:
    explicit BackendConnectionGuard(std::shared_ptr<BackendServer> backend)
        : backend_(std::move(backend)) {
        if (backend_) {
            backend_->active_connections.fetch_add(1, std::memory_order_relaxed);
            backend_->total_requests.fetch_add(1, std::memory_order_relaxed);
        }
    }

    ~BackendConnectionGuard() {
        if (backend_) {
            uint32_t cur = backend_->active_connections.load(std::memory_order_relaxed);
            while (cur > 0 && !backend_->active_connections.compare_exchange_weak(
                   cur, cur - 1, std::memory_order_relaxed)) {}
        }
    }

    BackendConnectionGuard(const BackendConnectionGuard&) = delete;
    BackendConnectionGuard& operator=(const BackendConnectionGuard&) = delete;

private:
    std::shared_ptr<BackendServer> backend_;
};

} // namespace balancer
