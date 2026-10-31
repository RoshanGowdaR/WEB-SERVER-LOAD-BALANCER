#include "server/server.h"
#include "balancer/round_robin.h"
#include "balancer/least_connections.h"
#include "logging/logger.h"
#include <chrono>

namespace server {

Server::Server(config::ServerConfig config)
    : config_(std::move(config)) {
    // 1. Initialize Thread Pool
    thread_pool_ = std::make_shared<threading::ThreadPool>(
        config_.worker_threads,
        config_.max_queue_size
    );

    // 2. Instantiate Backend servers
    std::vector<std::shared_ptr<balancer::BackendServer>> backend_nodes;
    for (const auto& b : config_.backends) {
        auto node = std::make_shared<balancer::BackendServer>();
        node->host = b.host;
        node->port = b.port;
        node->weight = b.weight;
        backend_nodes.push_back(node);
    }

    // 3. Initialize Load Balancer
    if (config_.algorithm == "least_connections") {
        load_balancer_ = std::make_shared<balancer::LeastConnectionsBalancer>(backend_nodes);
        LOG_INFO("Configured Load Balancing algorithm: Least Connections");
    } else {
        load_balancer_ = std::make_shared<balancer::RoundRobinBalancer>(backend_nodes);
        LOG_INFO("Configured Load Balancing algorithm: Round Robin");
    }

    // 4. Initialize Health Checker
    health_checker_ = std::make_shared<health::HealthChecker>(
        backend_nodes,
        config_.health_check_interval
    );

    // 5. Initialize Reverse Proxy
    proxy_ = std::make_shared<proxy::ReverseProxy>(load_balancer_, config_);

    // 6. Initialize TCP Server
    tcp_server_ = std::make_unique<TcpServer>(config_, thread_pool_, proxy_);
}

Server::~Server() {
    stop();
}

bool Server::start() {
    if (is_running_.exchange(true)) return true;

    LOG_INFO("Starting High-Performance Load Balancer Server...");
    health_checker_->start();

    if (!tcp_server_->start()) {
        LOG_ERROR("Failed to start TCP Server.");
        stop();
        return false;
    }

    LOG_INFO("Server startup sequence completed successfully.");
    return true;
}

void Server::stop() {
    if (!is_running_.exchange(false)) return;

    LOG_INFO("Initiating graceful shutdown sequence...");
    if (tcp_server_) {
        tcp_server_->stop();
    }

    if (health_checker_) {
        health_checker_->stop();
    }

    if (thread_pool_) {
        thread_pool_->shutdown(true);
    }

    LOG_INFO("Shutdown sequence finished.");
}

void Server::wait() {
    while (is_running_.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

} // namespace server
