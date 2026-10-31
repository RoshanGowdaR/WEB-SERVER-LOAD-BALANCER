#include "health/health_checker.h"
#include "network/socket.h"
#include "logging/logger.h"
#include <chrono>

namespace health {

HealthChecker::HealthChecker(
    std::vector<std::shared_ptr<balancer::BackendServer>> backends,
    std::chrono::milliseconds interval,
    std::chrono::milliseconds timeout,
    uint32_t failure_threshold,
    uint32_t success_threshold
) : backends_(std::move(backends)),
    interval_(interval),
    timeout_(timeout),
    failure_threshold_(failure_threshold),
    success_threshold_(success_threshold) {}

HealthChecker::~HealthChecker() {
    stop();
}

void HealthChecker::start() {
    if (running_.exchange(true)) return;
    worker_thread_ = std::thread(&HealthChecker::run_loop, this);
    LOG_INFO("HealthChecker background service started. Interval: " << interval_.count() << "ms");
}

void HealthChecker::stop() {
    if (!running_.exchange(false)) return;
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    LOG_INFO("HealthChecker background service stopped.");
}

void HealthChecker::run_loop() {
    while (running_.load()) {
        check_all_backends();

        // Sleep with fine-grained cancellation checks
        auto start_sleep = std::chrono::steady_clock::now();
        while (running_.load() && (std::chrono::steady_clock::now() - start_sleep < interval_)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}

void HealthChecker::check_all_backends() {
    for (auto& backend : backends_) {
        if (!backend) continue;

        bool healthy_now = check_backend(*backend);
        bool was_healthy = backend->is_healthy.load();

        if (healthy_now) {
            backend->consecutive_failures.store(0);
            uint32_t succ = ++backend->consecutive_successes;
            if (!was_healthy && succ >= success_threshold_) {
                backend->is_healthy.store(true);
                LOG_INFO("[HealthChecker] Backend RECOVERED: " << backend->address()
                         << " (consecutive successes: " << succ << ")");
            }
        } else {
            backend->consecutive_successes.store(0);
            uint32_t fails = ++backend->consecutive_failures;
            backend->total_failures++;
            if (was_healthy && fails >= failure_threshold_) {
                backend->is_healthy.store(false);
                LOG_WARN("[HealthChecker] Backend UNHEALTHY: " << backend->address()
                         << " (consecutive failures: " << fails << ")");
            }
        }
    }
}

bool HealthChecker::check_backend(balancer::BackendServer& backend) {
    net::ClientSocket sock;
    if (!sock.connect(backend.host, backend.port, timeout_)) {
        return false;
    }

    // Send HTTP health probe request
    std::string probe = "GET /health HTTP/1.1\r\nHost: " + backend.address() +
                        "\r\nUser-Agent: CppLoadBalancer-HealthChecker/1.0\r\nConnection: close\r\n\r\n";
    if (sock.send_all(probe.data(), probe.size()) <= 0) {
        return false;
    }

    char buffer[256];
    sock.set_receive_timeout(timeout_);
    ssize_t bytes = sock.receive(buffer, sizeof(buffer) - 1);
    if (bytes <= 0) {
        // If server connected but closed without response, fallback: socket connection succeeded
        return true;
    }

    buffer[bytes] = '\0';
    std::string_view resp(buffer, bytes);

    // Accept 200 OK, 204 No Content, or any 2xx / 3xx response as healthy
    if (resp.find("200") != std::string_view::npos ||
        resp.find("204") != std::string_view::npos ||
        resp.find("HTTP/1.1 2") != std::string_view::npos) {
        return true;
    }

    // If backend returns 500 or 503 on /health, consider it unhealthy
    if (resp.find("500") != std::string_view::npos || resp.find("503") != std::string_view::npos) {
        return false;
    }

    return true;
}

} // namespace health
