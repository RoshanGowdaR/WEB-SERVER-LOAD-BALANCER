#pragma once

#include "http/request.h"
#include "http/response.h"
#include "balancer/load_balancer.h"
#include "config/config.h"
#include "network/socket.h"
#include <memory>
#include <chrono>

namespace proxy {

class ReverseProxy {
public:
    ReverseProxy(
        std::shared_ptr<balancer::ILoadBalancer> load_balancer,
        config::ServerConfig config
    );

    ~ReverseProxy() = default;

    /**
     * @brief Process an incoming HTTP request from a client.
     * Checks internal endpoints (/health, /metrics, /backends) first;
     * otherwise routes to selected backend via load balancer.
     * Optionally takes client socket and buffer to stream Expect: 100-continue exchanges.
     */
    [[nodiscard]] http::HttpResponse handle_request(
        const http::HttpRequest& request,
        const std::string& client_ip,
        net::Socket* client_sock = nullptr,
        std::string* client_read_buf = nullptr
    );

    [[nodiscard]] std::shared_ptr<balancer::ILoadBalancer> get_load_balancer() const noexcept {
        return load_balancer_;
    }

private:
    [[nodiscard]] http::HttpResponse forward_to_backend(
        const http::HttpRequest& request,
        const std::shared_ptr<balancer::BackendServer>& backend,
        const std::string& client_ip,
        net::Socket* client_sock = nullptr,
        std::string* client_read_buf = nullptr
    );

    std::shared_ptr<balancer::ILoadBalancer> load_balancer_;
    config::ServerConfig config_;
};

} // namespace proxy
