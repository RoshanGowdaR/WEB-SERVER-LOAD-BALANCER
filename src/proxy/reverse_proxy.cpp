#include "proxy/reverse_proxy.h"
#include "monitoring/metrics.h"
#include "network/socket.h"
#include "http/http_parser.h"
#include "logging/logger.h"
#include <chrono>

namespace proxy {

ReverseProxy::ReverseProxy(
    std::shared_ptr<balancer::ILoadBalancer> load_balancer,
    config::ServerConfig config
) : load_balancer_(std::move(load_balancer)),
    config_(std::move(config)) {}

http::HttpResponse ReverseProxy::handle_request(
    const http::HttpRequest& request,
    const std::string& client_ip
) {
    auto start_time = std::chrono::steady_clock::now();
    monitoring::Metrics::instance().record_request_start();

    // 1. Intercept internal administrative / observability endpoints
    if (request.path == "/health") {
        auto backends = load_balancer_ ? load_balancer_->get_backends() : std::vector<std::shared_ptr<balancer::BackendServer>>{};
        std::string json = monitoring::Metrics::instance().to_health_json(backends);
        auto resp = http::HttpResponse::make_json(http::StatusCode::OK, json);
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
        monitoring::Metrics::instance().record_request_end(true, elapsed);
        return resp;
    }

    if (request.path == "/metrics") {
        auto backends = load_balancer_ ? load_balancer_->get_backends() : std::vector<std::shared_ptr<balancer::BackendServer>>{};
        std::string metrics_txt = monitoring::Metrics::instance().to_prometheus_format(backends);
        auto resp = http::HttpResponse::make_text(http::StatusCode::OK, metrics_txt);
        resp.set_header("Content-Type", "text/plain; version=0.0.4; charset=utf-8");
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
        monitoring::Metrics::instance().record_request_end(true, elapsed);
        return resp;
    }

    if (request.path == "/backends") {
        auto backends = load_balancer_ ? load_balancer_->get_backends() : std::vector<std::shared_ptr<balancer::BackendServer>>{};
        std::string json = monitoring::Metrics::instance().to_backends_json(backends);
        auto resp = http::HttpResponse::make_json(http::StatusCode::OK, json);
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
        monitoring::Metrics::instance().record_request_end(true, elapsed);
        return resp;
    }

    // 2. Select backend server
    if (!load_balancer_) {
        LOG_ERROR("Load balancer is uninitialized.");
        auto resp = http::HttpResponse::make_error(http::StatusCode::ServiceUnavailable, "Load balancer unavailable");
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
        monitoring::Metrics::instance().record_request_end(false, elapsed);
        return resp;
    }

    auto backend = load_balancer_->select_backend(request);
    if (!backend) {
        LOG_WARN("No healthy backends available to serve request: " << request.uri);
        auto resp = http::HttpResponse::make_error(
            http::StatusCode::ServiceUnavailable,
            "No healthy upstream backends available"
        );
        auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
        monitoring::Metrics::instance().record_request_end(false, elapsed);
        return resp;
    }

    // 3. Forward request to backend
    http::HttpResponse resp = forward_to_backend(request, backend, client_ip);

    bool is_success = (static_cast<int>(resp.status_code) < 500);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
    monitoring::Metrics::instance().record_request_end(is_success, elapsed);

    return resp;
}

http::HttpResponse ReverseProxy::forward_to_backend(
    const http::HttpRequest& request,
    const std::shared_ptr<balancer::BackendServer>& backend,
    const std::string& client_ip
) {
    // RAII guard for tracking active connections on the backend
    balancer::BackendConnectionGuard conn_guard(backend);

    net::ClientSocket backend_sock;
    if (!backend_sock.connect(backend->host, backend->port, config_.backend_timeout)) {
        backend->total_failures++;
        LOG_ERROR("Failed to connect to backend: " << backend->address());
        return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Failed to connect to upstream server");
    }

    // Clone request and inject proxy headers
    http::HttpRequest fwd_req = request;
    fwd_req.append_header_value("X-Forwarded-For", client_ip);
    fwd_req.set_header("X-Forwarded-Proto", "http");
    if (!fwd_req.has_header("X-Forwarded-Host")) {
        auto host_hdr = request.get_header("Host").value_or(config_.host + ":" + std::to_string(config_.port));
        fwd_req.set_header("X-Forwarded-Host", host_hdr);
    }
    fwd_req.set_header("Via", "1.1 cpp-load-balancer");
    // Force backend connection close to keep request lifecycle simple and clean
    fwd_req.set_header("Connection", "close");

    std::string req_str = fwd_req.to_string();
    backend_sock.set_send_timeout(config_.backend_timeout);
    if (backend_sock.send_all(req_str.data(), req_str.size()) <= 0) {
        backend->total_failures++;
        LOG_ERROR("Failed to send request payload to backend: " << backend->address());
        return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Upstream write error");
    }

    // Read response from backend
    backend_sock.set_receive_timeout(config_.backend_timeout);
    std::string response_buf;
    char buffer[4096];
    http::HttpParser parser(config_.max_header_size, config_.max_body_size);
    http::HttpResponse backend_response;

    while (true) {
        ssize_t bytes_read = backend_sock.receive(buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_ERROR("Timeout waiting for response from backend: " << backend->address());
                return http::HttpResponse::make_error(http::StatusCode::GatewayTimeout, "Gateway timeout reading response");
            }
            break; // Read error or connection terminated
        }
        if (bytes_read == 0) {
            // EOF: Backend finished transmitting
            break;
        }

        response_buf.append(buffer, static_cast<size_t>(bytes_read));

        size_t consumed = 0;
        http::ParseResult res = parser.parse_response(response_buf, backend_response, consumed);
        if (res == http::ParseResult::Complete) {
            return backend_response;
        }
        if (res == http::ParseResult::Error) {
            LOG_ERROR("Malformed response received from backend: " << backend->address());
            return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Malformed response from upstream server");
        }
    }

    // Attempt final parse if EOF reached
    size_t consumed = 0;
    http::ParseResult res = parser.parse_response(response_buf, backend_response, consumed, true);
    if (res == http::ParseResult::Complete) {
        return backend_response;
    }

    LOG_ERROR("Truncated or incomplete response from backend: " << backend->address());
    return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Incomplete response from upstream server");
}

} // namespace proxy
