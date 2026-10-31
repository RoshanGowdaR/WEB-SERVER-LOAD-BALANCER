#pragma once

#include "balancer/load_balancer.h"
#include <vector>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>

namespace health {

class HealthChecker {
public:
    HealthChecker(
        std::vector<std::shared_ptr<balancer::BackendServer>> backends,
        std::chrono::milliseconds interval = std::chrono::milliseconds(5000),
        std::chrono::milliseconds timeout = std::chrono::milliseconds(2000),
        uint32_t failure_threshold = 3,
        uint32_t success_threshold = 2
    );

    ~HealthChecker();

    HealthChecker(const HealthChecker&) = delete;
    HealthChecker& operator=(const HealthChecker&) = delete;

    void start();
    void stop();

    // Perform an immediate single-pass check on all backends (useful for testing or initial warmup)
    void check_all_backends();

    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

private:
    void run_loop();
    bool check_backend(balancer::BackendServer& backend);

    std::vector<std::shared_ptr<balancer::BackendServer>> backends_;
    std::chrono::milliseconds interval_;
    std::chrono::milliseconds timeout_;
    uint32_t failure_threshold_;
    uint32_t success_threshold_;

    std::atomic<bool> running_{false};
    std::thread worker_thread_;
};

} // namespace health
