#pragma once

#include "network/socket.h"
#include "threading/thread_pool.h"
#include "proxy/reverse_proxy.h"
#include "config/config.h"
#include <memory>
#include <atomic>
#include <functional>

namespace server {

class TcpServer {
public:
    TcpServer(
        config::ServerConfig config,
        std::shared_ptr<threading::ThreadPool> thread_pool,
        std::shared_ptr<proxy::ReverseProxy> proxy
    );

    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    bool start();
    void stop();

    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }
    [[nodiscard]] uint16_t get_port() const noexcept { return listening_socket_.get_bound_port(); }

private:
    void accept_loop();
    void handle_client(net::Socket client_sock, std::string client_ip, uint16_t client_port);

    config::ServerConfig config_;
    std::shared_ptr<threading::ThreadPool> thread_pool_;
    std::shared_ptr<proxy::ReverseProxy> proxy_;

    net::ServerSocket listening_socket_;
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
};

} // namespace server
