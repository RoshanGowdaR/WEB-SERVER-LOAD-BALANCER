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
