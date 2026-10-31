#include "monitoring/metrics.h"
#include <sstream>
#include <algorithm>
#include <iomanip>

namespace monitoring {

Metrics& Metrics::instance() {
    static Metrics inst;
    return inst;
}

void Metrics::reset() noexcept {
    total_requests_.store(0);
    successful_requests_.store(0);
    failed_requests_.store(0);
    active_connections_.store(0);
    rejected_requests_.store(0);
    total_latency_us_.store(0);
    max_latency_us_.store(0);

    std::lock_guard<std::mutex> lock(latency_mutex_);
    latency_samples_.clear();
    sample_index_ = 0;
}

void Metrics::record_request_start() noexcept {
    total_requests_++;
    active_connections_++;
}

void Metrics::record_request_end(bool success, std::chrono::microseconds latency) noexcept {
    active_connections_--;
    if (success) {
        successful_requests_++;
    } else {
        failed_requests_++;
    }

    uint64_t lat = static_cast<uint64_t>(latency.count());
    total_latency_us_ += lat;

    uint64_t cur_max = max_latency_us_.load(std::memory_order_relaxed);
    while (lat > cur_max && !max_latency_us_.compare_exchange_weak(cur_max, lat, std::memory_order_relaxed)) {}

    std::lock_guard<std::mutex> lock(latency_mutex_);
    if (latency_samples_.size() < LATENCY_WINDOW) {
        latency_samples_.push_back(lat);
    } else {
        latency_samples_[sample_index_ % LATENCY_WINDOW] = lat;
        sample_index_++;
    }
}

void Metrics::record_rejected_request() noexcept {
    rejected_requests_++;
    failed_requests_++;
}

double Metrics::average_latency_ms() const noexcept {
    uint64_t reqs = successful_requests_.load();
    if (reqs == 0) return 0.0;
    return static_cast<double>(total_latency_us_.load()) / static_cast<double>(reqs) / 1000.0;
}

double Metrics::max_latency_ms() const noexcept {
    return static_cast<double>(max_latency_us_.load()) / 1000.0;
}

double Metrics::percentile_latency_ms(double percentile) const {
    std::lock_guard<std::mutex> lock(latency_mutex_);
    if (latency_samples_.empty()) return 0.0;

    std::vector<uint64_t> sorted = latency_samples_;
    std::sort(sorted.begin(), sorted.end());

    size_t idx = static_cast<size_t>(percentile / 100.0 * static_cast<double>(sorted.size() - 1));
    return static_cast<double>(sorted[idx]) / 1000.0;
}

std::string Metrics::to_prometheus_format(const std::vector<std::shared_ptr<balancer::BackendServer>>& backends) const {
    std::ostringstream ss;
    ss << "# HELP loadbalancer_requests_total Total number of HTTP requests received\n"
       << "# TYPE loadbalancer_requests_total counter\n"
       << "loadbalancer_requests_total " << total_requests_.load() << "\n\n"

       << "# HELP loadbalancer_requests_successful_total Total successful requests forwarded\n"
       << "# TYPE loadbalancer_requests_successful_total counter\n"
       << "loadbalancer_requests_successful_total " << successful_requests_.load() << "\n\n"

       << "# HELP loadbalancer_requests_failed_total Total failed requests\n"
       << "# TYPE loadbalancer_requests_failed_total counter\n"
       << "loadbalancer_requests_failed_total " << failed_requests_.load() << "\n\n"

       << "# HELP loadbalancer_requests_rejected_total Total requests rejected due to overload\n"
       << "# TYPE loadbalancer_requests_rejected_total counter\n"
       << "loadbalancer_requests_rejected_total " << rejected_requests_.load() << "\n\n"

       << "# HELP loadbalancer_active_connections Current active connections\n"
       << "# TYPE loadbalancer_active_connections gauge\n"
       << "loadbalancer_active_connections " << active_connections_.load() << "\n\n"

       << "# HELP loadbalancer_latency_avg_ms Average request latency in milliseconds\n"
       << "# TYPE loadbalancer_latency_avg_ms gauge\n"
       << "loadbalancer_latency_avg_ms " << average_latency_ms() << "\n\n"

       << "# HELP loadbalancer_latency_max_ms Maximum observed latency in milliseconds\n"
       << "# TYPE loadbalancer_latency_max_ms gauge\n"
       << "loadbalancer_latency_max_ms " << max_latency_ms() << "\n\n"

       << "# HELP loadbalancer_latency_p95_ms 95th percentile latency in milliseconds\n"
       << "# TYPE loadbalancer_latency_p95_ms gauge\n"
       << "loadbalancer_latency_p95_ms " << percentile_latency_ms(95.0) << "\n\n"

       << "# HELP loadbalancer_latency_p99_ms 99th percentile latency in milliseconds\n"
       << "# TYPE loadbalancer_latency_p99_ms gauge\n"
       << "loadbalancer_latency_p99_ms " << percentile_latency_ms(99.0) << "\n\n";

    ss << "# HELP loadbalancer_backend_healthy Backend health status (1 = healthy, 0 = unhealthy)\n"
       << "# TYPE loadbalancer_backend_healthy gauge\n";
    for (const auto& b : backends) {
        if (!b) continue;
        ss << "loadbalancer_backend_healthy{backend=\"" << b->address() << "\"} "
           << (b->is_healthy.load() ? 1 : 0) << "\n";
    }
    ss << "\n";

    ss << "# HELP loadbalancer_backend_active_connections Active connections per backend\n"
       << "# TYPE loadbalancer_backend_active_connections gauge\n";
    for (const auto& b : backends) {
        if (!b) continue;
        ss << "loadbalancer_backend_active_connections{backend=\"" << b->address() << "\"} "
           << b->active_connections.load() << "\n";
    }
    ss << "\n";

    ss << "# HELP loadbalancer_backend_requests_total Total requests routed to backend\n"
       << "# TYPE loadbalancer_backend_requests_total counter\n";
    for (const auto& b : backends) {
        if (!b) continue;
        ss << "loadbalancer_backend_requests_total{backend=\"" << b->address() << "\"} "
           << b->total_requests.load() << "\n";
    }
    ss << "\n";

    return ss.str();
}

std::string Metrics::to_health_json(const std::vector<std::shared_ptr<balancer::BackendServer>>& backends) const {
    size_t healthy_count = 0;
    for (const auto& b : backends) {
        if (b && b->is_healthy.load()) healthy_count++;
    }

    bool overall_healthy = (healthy_count > 0 || backends.empty());
    std::ostringstream ss;
    ss << "{\n"
       << "  \"status\": \"" << (overall_healthy ? "UP" : "DEGRADED") << "\",\n"
       << "  \"healthy_backends\": " << healthy_count << ",\n"
       << "  \"total_backends\": " << backends.size() << ",\n"
       << "  \"active_connections\": " << active_connections_.load() << ",\n"
       << "  \"total_requests\": " << total_requests_.load() << "\n"
       << "}";
    return ss.str();
}

std::string Metrics::to_backends_json(const std::vector<std::shared_ptr<balancer::BackendServer>>& backends) const {
    std::ostringstream ss;
    ss << "[\n";
    for (size_t i = 0; i < backends.size(); ++i) {
        const auto& b = backends[i];
        if (!b) continue;
        ss << "  {\n"
           << "    \"address\": \"" << b->address() << "\",\n"
           << "    \"host\": \"" << b->host << "\",\n"
           << "    \"port\": " << b->port << ",\n"
           << "    \"healthy\": " << (b->is_healthy.load() ? "true" : "false") << ",\n"
           << "    \"active_connections\": " << b->active_connections.load() << ",\n"
           << "    \"total_requests\": " << b->total_requests.load() << ",\n"
           << "    \"total_failures\": " << b->total_failures.load() << "\n"
           << "  }" << (i + 1 < backends.size() ? ",\n" : "\n");
    }
    ss << "]";
    return ss.str();
}

} // namespace monitoring
