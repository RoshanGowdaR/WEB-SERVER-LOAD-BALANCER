#include <gtest/gtest.h>
#include "monitoring/metrics.h"
#include <chrono>

TEST(MetricsTest, RecordAndAggregateMetrics) {
    auto& metrics = monitoring::Metrics::instance();
    metrics.reset();

    EXPECT_EQ(metrics.total_requests(), 0);
    EXPECT_EQ(metrics.active_connections(), 0);

    metrics.record_request_start();
    EXPECT_EQ(metrics.total_requests(), 1);
    EXPECT_EQ(metrics.active_connections(), 1);

    metrics.record_request_end(true, std::chrono::microseconds(10000)); // 10ms
    EXPECT_EQ(metrics.successful_requests(), 1);
    EXPECT_EQ(metrics.active_connections(), 0);
    EXPECT_NEAR(metrics.average_latency_ms(), 10.0, 0.001);

    metrics.record_rejected_request();
    EXPECT_EQ(metrics.rejected_requests(), 1);
    EXPECT_EQ(metrics.failed_requests(), 1);
}

TEST(MetricsTest, PrometheusAndJsonExport) {
    auto& metrics = monitoring::Metrics::instance();
    metrics.reset();

    auto b1 = std::make_shared<balancer::BackendServer>();
    b1->host = "127.0.0.1"; b1->port = 9001; b1->is_healthy.store(true);
    std::vector<std::shared_ptr<balancer::BackendServer>> backends = {b1};

    metrics.record_request_start();
    metrics.record_request_end(true, std::chrono::microseconds(5000));

    std::string prom = metrics.to_prometheus_format(backends);
    EXPECT_NE(prom.find("loadbalancer_requests_total 1"), std::string::npos);
    EXPECT_NE(prom.find("loadbalancer_backend_healthy{backend=\"127.0.0.1:9001\"} 1"), std::string::npos);

    std::string health_json = metrics.to_health_json(backends);
    EXPECT_NE(health_json.find("\"status\": \"UP\""), std::string::npos);
    EXPECT_NE(health_json.find("\"healthy_backends\": 1"), std::string::npos);

    std::string backends_json = metrics.to_backends_json(backends);
    EXPECT_NE(backends_json.find("\"address\": \"127.0.0.1:9001\""), std::string::npos);
}
