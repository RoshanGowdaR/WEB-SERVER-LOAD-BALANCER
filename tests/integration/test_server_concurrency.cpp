#include <gtest/gtest.h>
#include "server/server.h"
#include "network/socket.h"
#include <thread>
#include <vector>
#include <atomic>

TEST(ServerConcurrencyTest, HandleMultipleConcurrentClients) {
    config::ServerConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = 18088;
    cfg.worker_threads = 4;
    cfg.max_queue_size = 500;
    cfg.backends = {{"127.0.0.1", 19991, 1}};

    server::Server srv(cfg);
    ASSERT_TRUE(srv.start());

    constexpr int NUM_CLIENTS = 20;
    std::atomic<int> successful_requests{0};
    std::vector<std::thread> client_threads;

    for (int i = 0; i < NUM_CLIENTS; ++i) {
        client_threads.emplace_back([&successful_requests]() {
            net::ClientSocket client;
            if (client.connect("127.0.0.1", 18088, std::chrono::milliseconds(2000))) {
                std::string req = "GET /health HTTP/1.1\r\nHost: 127.0.0.1:18088\r\nConnection: close\r\n\r\n";
                if (client.send_string(req) > 0) {
                    char buf[1024];
                    ssize_t bytes = client.receive(buf, sizeof(buf) - 1);
                    if (bytes > 0) {
                        buf[bytes] = '\0';
                        std::string resp(buf, bytes);
                        if (resp.find("200 OK") != std::string::npos && resp.find("\"status\":") != std::string::npos) {
                            successful_requests.fetch_add(1);
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

    EXPECT_EQ(successful_requests.load(), NUM_CLIENTS);
}
