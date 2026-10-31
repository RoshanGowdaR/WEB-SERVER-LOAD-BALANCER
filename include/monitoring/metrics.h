#pragma once

#include "balancer/load_balancer.h"
#include <string>
#include <atomic>
#include <vector>
#include <memory>
#include <mutex>
#include <chrono>

namespace monitoring {

class Metrics {
public:
    static Metrics& instance();

    void record_request_start() noexcept;
    void record_request_end(bool success, std::chrono::microseconds latency) noexcept;
    void record_rejected_request() noexcept;

    [[nodiscard]] uint64_t total_requests() const noexcept { return total_requests_.load(); }
    [[nodiscard]] uint64_t successful_requests() const noexcept { return successful_requests_.load(); }
    [[nodiscard]] uint64_t failed_requests() const noexcept { return failed_requests_.load(); }
    [[nodiscard]] uint32_t active_connections() const noexcept { return active_connections_.load(); }
    [[nodiscard]] uint64_t rejected_requests() const noexcept { return rejected_requests_.load(); }

    [[nodiscard]] double average_latency_ms() const noexcept;
    [[nodiscard]] double max_latency_ms() const noexcept;
    [[nodiscard]] double percentile_latency_ms(double percentile) const;

    // Export formats
    [[nodiscard]] std::string to_prometheus_format(const std::vector<std::shared_ptr<balancer::BackendServer>>& backends) const;
    [[nodiscard]] std::string to_health_json(const std::vector<std::shared_ptr<balancer::BackendServer>>& backends) const;
    [[nodiscard]] std::string to_backends_json(const std::vector<std::shared_ptr<balancer::BackendServer>>& backends) const;

    void reset() noexcept;

private:
    Metrics() = default;
    ~Metrics() = default;

    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> successful_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};
    std::atomic<uint32_t> active_connections_{0};
    std::atomic<uint64_t> rejected_requests_{0};

    std::atomic<uint64_t> total_latency_us_{0};
    std::atomic<uint64_t> max_latency_us_{0};

    // Circular latency sample buffer for percentiles
    static constexpr size_t LATENCY_WINDOW = 2048;
    mutable std::mutex latency_mutex_;
    std::vector<uint64_t> latency_samples_;
    size_t sample_index_{0};
};

} // namespace monitoring
