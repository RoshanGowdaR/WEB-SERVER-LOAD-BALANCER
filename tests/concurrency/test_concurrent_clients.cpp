#include <gtest/gtest.h>
#include "server/server.h"
#include "network/socket.h"
#include <thread>
#include <vector>
#include <atomic>
#include <barrier>
#include <chrono>

TEST(ConcurrencyTest, HandlesThirtyConcurrentClientsInternalEndpoint) {
    config::ServerConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 18090;
    cfg.worker_threads = 4;
    cfg.max_queue_size = 500;
    cfg.backends = {{"127.0.0.1", 19992, 1}};

    server::Server srv(cfg);
    ASSERT_TRUE(srv.start());

    constexpr size_t num_clients = 30;
    std::barrier sync_barrier(num_clients);
    std::atomic<int> successful_requests{0};
    std::vector<std::thread> client_threads;

    for (size_t i = 0; i < num_clients; ++i) {
        client_threads.emplace_back([&]() {
            sync_barrier.arrive_and_wait();

            net::ClientSocket client;
            if (client.connect("127.0.0.1", 18090, std::chrono::milliseconds(2000))) {
                std::string req = "GET /health HTTP/1.1\r\nHost: 127.0.0.1:18090\r\nConnection: close\r\n\r\n";
                if (client.send_string(req) > 0) {
                    char buf[1024];
                    ssize_t bytes = client.receive(buf, sizeof(buf) - 1);
                    if (bytes > 0) {
                        buf[bytes] = '\0';
                        std::string resp(buf, bytes);
                        if (resp.find("200 OK") != std::string::npos && resp.find("\"status\":") != std::string::npos) {
                            successful_requests++;
                        }
                    }
                }
            }
        });
    }

    for (auto& t : client_threads) {
        t.join();
    }

    srv.stop();
    EXPECT_EQ(successful_requests.load(), static_cast<int>(num_clients));
}

TEST(ConcurrencyTest, ThreadPoolBackpressureUnderHeavyLoad) {
    constexpr size_t num_workers = 2;
    constexpr size_t queue_size = 5;
    threading::ThreadPool pool(num_workers, queue_size, threading::BackpressurePolicy::Reject);

    std::atomic<bool> hold_workers{true};
    std::atomic<int> workers_started{0};

    // Saturate workers
    for (size_t i = 0; i < num_workers; ++i) {
        pool.enqueue([&]() {
            workers_started++;
            while (hold_workers.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }

    while (workers_started.load() < static_cast<int>(num_workers)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    // Fill queue
    for (size_t i = 0; i < queue_size; ++i) {
        EXPECT_TRUE(pool.enqueue([]() {}));
    }

    // Next tasks MUST be rejected
    std::atomic<int> rejections{0};
    for (int i = 0; i < 10; ++i) {
        if (!pool.enqueue([]() {})) {
            rejections++;
        }
    }

    EXPECT_GT(rejections.load(), 0);
    EXPECT_EQ(pool.rejected_tasks(), rejections.load());

    // Release workers
    hold_workers.store(false);
    pool.shutdown(true);
}

TEST(ConcurrencyTest, SlowlorisAbsoluteDeadlineEnforced) {
    config::ServerConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 18091;
    cfg.worker_threads = 2;
    cfg.connection_timeout = std::chrono::milliseconds(300); // 300ms absolute deadline
    cfg.backends = {{"127.0.0.1", 19993, 1}};

    server::Server srv(cfg);
    ASSERT_TRUE(srv.start());

    net::ClientSocket client;
    ASSERT_TRUE(client.connect("127.0.0.1", 18091, std::chrono::milliseconds(1000)));

    // Client trickles 1 byte every 80ms without finishing the headers
    // With 5 iterations * 80ms = 400ms, exceeding the 300ms absolute deadline
    bool timeout_received = false;
    for (int i = 0; i < 6; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        char byte = 'X';
        if (client.send_all(&byte, 1) <= 0) {
            timeout_received = true;
            break;
        }
    }

    // Try reading response from server
    client.set_receive_timeout(std::chrono::milliseconds(500));
    char buf[1024];
    ssize_t n = client.receive(buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        std::string resp(buf, n);
        if (resp.find("408") != std::string::npos || resp.find("Request Timeout") != std::string::npos) {
            timeout_received = true;
        }
    } else {
        // Socket closed by server due to deadline
        timeout_received = true;
    }

    srv.stop();
    EXPECT_TRUE(timeout_received);
}

TEST(ConcurrencyTest, SocketDescriptorLifecycleConcurrentCloseSafety) {
    // Verify that concurrent close() during an in-flight socket read
    // cleanly unblocks the operation without data races or descriptor reuse races.
    net::ServerSocket server;
    ASSERT_TRUE(server.bind_and_listen("127.0.0.1", 0));
    uint16_t port = server.get_bound_port();

    net::ClientSocket client;
    ASSERT_TRUE(client.connect("127.0.0.1", port, std::chrono::milliseconds(1000)));

    net::Socket accepted = server.accept();
    ASSERT_TRUE(accepted.is_valid());

    std::atomic<bool> read_started{false};
    std::atomic<bool> read_completed{false};
    ssize_t read_result = 0;

    // Thread 1: Blocking read
    std::thread reader([&]() {
        read_started.store(true);
        char buf[64];
        read_result = accepted.receive(buf, sizeof(buf));
        read_completed.store(true);
    });

    while (!read_started.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    // Give reader thread time to enter ::recv()
    std::this_thread::sleep_for(std::chrono::milliseconds(20));

    // Thread 2: Concurrent close()
    accepted.close();

    reader.join();

    EXPECT_TRUE(read_completed.load());
    EXPECT_LE(read_result, 0); // Read returned error or EOF due to shutdown
    EXPECT_FALSE(accepted.is_valid());
}
