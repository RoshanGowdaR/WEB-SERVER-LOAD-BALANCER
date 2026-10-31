#include <gtest/gtest.h>
#include "proxy/reverse_proxy.h"
#include "balancer/round_robin.h"
#include "network/socket.h"
#include <thread>
#include <atomic>

TEST(ReverseProxyTest, InterceptsInternalEndpoints) {
    auto b1 = std::make_shared<balancer::BackendServer>();
    b1->host = "127.0.0.1"; b1->port = 9999;
    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{b1});

    config::ServerConfig cfg;
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest req_health;
    req_health.path = "/health";
    auto resp_health = proxy.handle_request(req_health, "127.0.0.1");
    EXPECT_EQ(resp_health.status_code, http::StatusCode::OK);
    EXPECT_NE(resp_health.body.find("\"status\": \"UP\""), std::string::npos);

    http::HttpRequest req_metrics;
    req_metrics.path = "/metrics";
    auto resp_metrics = proxy.handle_request(req_metrics, "127.0.0.1");
    EXPECT_EQ(resp_metrics.status_code, http::StatusCode::OK);
    EXPECT_NE(resp_metrics.body.find("loadbalancer_requests_total"), std::string::npos);

    http::HttpRequest req_backends;
    req_backends.path = "/backends";
    auto resp_backends = proxy.handle_request(req_backends, "127.0.0.1");
    EXPECT_EQ(resp_backends.status_code, http::StatusCode::OK);
    EXPECT_NE(resp_backends.body.find("\"address\": \"127.0.0.1:9999\""), std::string::npos);
}

TEST(ReverseProxyTest, ForwardsRequestToMockBackend) {
    // 1. Setup mock backend TCP server
    net::ServerSocket mock_backend;
    ASSERT_TRUE(mock_backend.bind_and_listen("127.0.0.1", 0));
    uint16_t backend_port = mock_backend.get_bound_port();

    std::atomic<bool> backend_received{false};
    std::string received_x_forwarded_for;

    std::thread backend_thread([&]() {
        net::Socket client = mock_backend.accept();
        ASSERT_TRUE(client.is_valid());

        char buf[2048];
        ssize_t bytes = client.receive(buf, sizeof(buf) - 1);
        if (bytes > 0) {
            buf[bytes] = '\0';
            std::string raw(buf, bytes);
            if (raw.find("X-Forwarded-For: 192.168.1.50") != std::string::npos) {
                received_x_forwarded_for = "192.168.1.50";
            }
            backend_received.store(true);
        }

        std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\nHello Backend";
        client.send_string(reply);
    });

    // 2. Setup ReverseProxy pointing to mock backend
    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = backend_port;

    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(2000);
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest client_req;
    client_req.method = http::Method::GET;
    client_req.uri = "/api/data";
    client_req.path = "/api/data";

    auto resp = proxy.handle_request(client_req, "192.168.1.50");

    backend_thread.join();

    EXPECT_TRUE(backend_received.load());
    EXPECT_EQ(received_x_forwarded_for, "192.168.1.50");
    EXPECT_EQ(resp.status_code, http::StatusCode::OK);
    EXPECT_EQ(resp.body, "Hello Backend");
}

TEST(ReverseProxyTest, HandlesBackendDownReturnsBadGateway) {
    auto node = std::make_shared<balancer::BackendServer>();
    node->host = "127.0.0.1";
    node->port = 65432; // Unused port where nothing is listening

    auto lb = std::make_shared<balancer::RoundRobinBalancer>(std::vector<std::shared_ptr<balancer::BackendServer>>{node});
    config::ServerConfig cfg;
    cfg.backend_timeout = std::chrono::milliseconds(200);
    proxy::ReverseProxy proxy(lb, cfg);

    http::HttpRequest client_req;
    client_req.method = http::Method::GET;
    client_req.uri = "/test";
    client_req.path = "/test";

    auto resp = proxy.handle_request(client_req, "127.0.0.1");
    EXPECT_EQ(resp.status_code, http::StatusCode::BadGateway);
}
