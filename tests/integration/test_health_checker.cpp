#include <gtest/gtest.h>
#include "health/health_checker.h"
#include "network/socket.h"
#include <thread>
#include <atomic>

TEST(HealthCheckerTest, ActiveHealthDetectionAndFailureHysteresis) {
    auto backend = std::make_shared<balancer::BackendServer>();
    backend->host = "127.0.0.1";
    backend->port = 65431; // Dead port
    backend->is_healthy.store(true);

    // Failure threshold: 2, Success threshold: 1
    health::HealthChecker checker(
        {backend},
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(100),
        2, 1
    );

    // Pass 1: 1 failure (still healthy due to threshold 2)
    checker.check_all_backends();
    EXPECT_TRUE(backend->is_healthy.load());
    EXPECT_EQ(backend->consecutive_failures.load(), 1);

    // Pass 2: 2nd failure -> transitions to unhealthy!
    checker.check_all_backends();
    EXPECT_FALSE(backend->is_healthy.load());
    EXPECT_EQ(backend->consecutive_failures.load(), 2);
}

TEST(HealthCheckerTest, BackendRecoveryHysteresis) {
    // Start mock backend server
    net::ServerSocket mock_server;
    ASSERT_TRUE(mock_server.bind_and_listen("127.0.0.1", 0));
    uint16_t port = mock_server.get_bound_port();

    std::atomic<bool> keep_serving{true};
    std::thread server_thread([&]() {
        while (keep_serving.load()) {
            net::Socket client = mock_server.accept();
            if (!client.is_valid()) break;

            char buf[512];
            client.receive(buf, sizeof(buf));
            std::string reply = "HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nOK";
            client.send_string(reply);
        }
    });

    auto backend = std::make_shared<balancer::BackendServer>();
    backend->host = "127.0.0.1";
    backend->port = port;
    backend->is_healthy.store(false); // Initially marked unhealthy

    // Requires 2 consecutive successes to recover
    health::HealthChecker checker(
        {backend},
        std::chrono::milliseconds(100),
        std::chrono::milliseconds(500),
        2, 2
    );

    // First check: 1 success, still unhealthy
    checker.check_all_backends();
    EXPECT_FALSE(backend->is_healthy.load());
    EXPECT_EQ(backend->consecutive_successes.load(), 1);

    // Second check: 2 successes -> recovered to healthy!
    checker.check_all_backends();
    EXPECT_TRUE(backend->is_healthy.load());

    keep_serving.store(false);
    {
        net::ClientSocket wake_sock;
        wake_sock.connect("127.0.0.1", port, std::chrono::milliseconds(200));
    }
    server_thread.join();
    mock_server.close();
}
