#pragma once

#include "config/config.h"
#include "threading/thread_pool.h"
#include "balancer/load_balancer.h"
#include "health/health_checker.h"
#include "proxy/reverse_proxy.h"
#include "server/tcp_server.h"
#include <memory>
#include <atomic>

namespace server {

class Server {
public:
    explicit Server(config::ServerConfig config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    bool start();
    void stop();
    void wait();

    [[nodiscard]] const config::ServerConfig& get_config() const noexcept { return config_; }
    [[nodiscard]] std::shared_ptr<balancer::ILoadBalancer> get_load_balancer() const noexcept { return load_balancer_; }
    [[nodiscard]] std::shared_ptr<threading::ThreadPool> get_thread_pool() const noexcept { return thread_pool_; }

private:
    config::ServerConfig config_;
    std::shared_ptr<threading::ThreadPool> thread_pool_;
    std::shared_ptr<balancer::ILoadBalancer> load_balancer_;
    std::shared_ptr<health::HealthChecker> health_checker_;
    std::shared_ptr<proxy::ReverseProxy> proxy_;
    std::unique_ptr<TcpServer> tcp_server_;

    std::atomic<bool> is_running_{false};
};

} // namespace server
