#include "proxy/reverse_proxy.h"
#include "monitoring/metrics.h"
#include "network/socket.h"
#include "http/http_parser.h"
#include "logging/logger.h"
#include <chrono>
#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace proxy {

namespace {

// Standard hop-by-hop headers per RFC 9110 Section 7.6.1
bool is_hop_by_hop_header(std::string_view name) {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    static const std::unordered_set<std::string_view> hop_by_hop = {
        "connection",
        "keep-alive",
        "proxy-authenticate",
        "proxy-authorization",
        "te",
        "trailer",
        "transfer-encoding",
        "upgrade"
    };

    return hop_by_hop.find(lower) != hop_by_hop.end();
}

std::vector<std::string> parse_connection_header_fields(std::string_view connection_val) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (start < connection_val.size()) {
        size_t comma = connection_val.find(',', start);
        std::string_view item = (comma == std::string_view::npos)
                                    ? connection_val.substr(start)
                                    : connection_val.substr(start, comma - start);

        while (!item.empty() && (item.front() == ' ' || item.front() == '\t')) item.remove_prefix(1);
        while (!item.empty() && (item.back() == ' ' || item.back() == '\t')) item.remove_suffix(1);

        if (!item.empty()) {
            std::string s(item);
            std::transform(s.begin(), s.end(), s.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            fields.push_back(std::move(s));
        }

        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return fields;
}

} // namespace

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
    // RAII guard for tracking active connections on the backend (with underflow protection)
    balancer::BackendConnectionGuard conn_guard(backend);

    net::ClientSocket backend_sock;
    if (!backend_sock.connect(backend->host, backend->port, config_.backend_timeout)) {
        backend->total_failures++;
        LOG_ERROR("Failed to connect to backend: " << backend->address());
        return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Failed to connect to upstream server");
    }

    // Build sanitized forwarded request
    http::HttpRequest fwd_req;
    fwd_req.method = request.method;
    fwd_req.uri = request.uri;
    fwd_req.path = request.path;
    fwd_req.query = request.query;
    fwd_req.version = "HTTP/1.1";
    fwd_req.body = request.body;

    // Collect headers nominated in client Connection header
    std::unordered_set<std::string> nominated_connection_headers;
    if (auto conn_val = request.get_header("Connection"); conn_val) {
        auto fields = parse_connection_header_fields(*conn_val);
        for (auto& f : fields) {
            nominated_connection_headers.insert(std::move(f));
        }
    }

    // Strip hop-by-hop headers and Connection-nominated headers
    for (const auto& [name, val] : request.headers) {
        if (is_hop_by_hop_header(name)) {
            continue;
        }

        std::string lower_name = name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (nominated_connection_headers.find(lower_name) != nominated_connection_headers.end()) {
            continue;
        }

        // Strip client-supplied X-Forwarded-For to prevent client spoofing
        if (lower_name == "x-forwarded-for") {
            continue;
        }

        fwd_req.set_header(name, val);
    }

    // Inject trusted proxy forwarding headers
    fwd_req.set_header("X-Forwarded-For", client_ip);
    fwd_req.set_header("X-Forwarded-Proto", "http");
    if (!fwd_req.has_header("X-Forwarded-Host")) {
        auto host_hdr = request.get_header("Host").value_or(config_.host + ":" + std::to_string(config_.port));
        fwd_req.set_header("X-Forwarded-Host", host_hdr);
    }

    if (auto existing_via = request.get_header("Via"); existing_via) {
        fwd_req.set_header("Via", *existing_via + ", 1.1 cpp-load-balancer");
    } else {
        fwd_req.set_header("Via", "1.1 cpp-load-balancer");
    }

    // Force backend connection close to keep request lifecycle simple and avoid desync bugs
    fwd_req.set_header("Connection", "close");

    std::string req_str = fwd_req.to_string();
    backend_sock.set_send_timeout(config_.backend_timeout);
    if (backend_sock.send_all(req_str.data(), req_str.size()) <= 0) {
        backend->total_failures++;
        LOG_ERROR("Failed to send request payload to backend: " << backend->address());
        return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Upstream write error");
    }

    // Read response from backend with strict bounds and error differentiation
    backend_sock.set_receive_timeout(config_.backend_timeout);
    std::string response_buf;
    char buffer[4096];
    http::HttpParser parser(config_.max_header_size, config_.max_body_size);
    http::HttpResponse backend_response;
    bool is_head = (request.method == http::Method::HEAD);

    const size_t max_total_allowed = config_.max_header_size + config_.max_body_size;
    bool genuine_eof = false;

    while (true) {
        ssize_t bytes_read = backend_sock.receive(buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_ERROR("Timeout waiting for response from backend: " << backend->address());
                return http::HttpResponse::make_error(http::StatusCode::GatewayTimeout, "Gateway timeout reading response from upstream");
            }
            // Socket read error (ECONNRESET, EPIPE, etc.) - NEVER treat as EOF!
            backend->total_failures++;
            LOG_ERROR("Socket error reading response from backend: " << backend->address());
            return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Network error reading response from upstream server");
        }

        if (bytes_read == 0) {
            // Genuine EOF: backend closed connection
            genuine_eof = true;
            break;
        }

        // Bounded buffering check: prevent memory exhaustion from runaway backends
        if (response_buf.size() + static_cast<size_t>(bytes_read) > max_total_allowed) {
            backend->total_failures++;
            LOG_ERROR("Backend response exceeded configured maximum size limit: " << backend->address());
            return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Backend response exceeded maximum allowed buffer limit");
        }

        response_buf.append(buffer, static_cast<size_t>(bytes_read));

        size_t consumed = 0;
        http::ParseResult res = parser.parse_response(response_buf, backend_response, consumed, false, is_head);
        if (res == http::ParseResult::Complete) {
            return backend_response;
        }
        if (res == http::ParseResult::Error) {
            backend->total_failures++;
            LOG_ERROR("Malformed response received from backend: " << backend->address() << " - " << parser.get_error_message());
            return http::HttpResponse::make_error(parser.get_error_status(), parser.get_error_message());
        }
    }

    // Only if genuine EOF occurred, attempt final parse for close-delimited response
    if (genuine_eof) {
        size_t consumed = 0;
        http::ParseResult res = parser.parse_response(response_buf, backend_response, consumed, true, is_head);
        if (res == http::ParseResult::Complete) {
            return backend_response;
        }
    }

    backend->total_failures++;
    LOG_ERROR("Truncated or incomplete response from backend: " << backend->address());
    return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Incomplete response from upstream server");
}

} // namespace proxy
