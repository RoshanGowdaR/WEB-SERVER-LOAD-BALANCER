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
    const std::string& client_ip,
    net::Socket* client_sock,
    std::string* client_read_buf
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
    http::HttpResponse resp = forward_to_backend(request, backend, client_ip, client_sock, client_read_buf);

    bool is_success = (static_cast<int>(resp.status_code) < 500);
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - start_time);
    monitoring::Metrics::instance().record_request_end(is_success, elapsed);

    return resp;
}

http::HttpResponse ReverseProxy::forward_to_backend(
    const http::HttpRequest& request,
    const std::shared_ptr<balancer::BackendServer>& backend,
    const std::string& client_ip,
    net::Socket* client_sock,
    std::string* client_read_buf
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

    bool streaming_body_after_100 = false;
    if (client_sock && request.expects_100_continue() && request.content_length() > request.body.size()) {
        streaming_body_after_100 = true;
        fwd_req.body.clear();
        fwd_req.set_header("Content-Length", std::to_string(request.content_length()));
        fwd_req.set_header("Expect", "100-continue");
    }

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

        // Process response, handling interim 1xx informational responses (100 Continue, 102, 103)
        while (!response_buf.empty()) {
            size_t consumed = 0;
            http::ParseResult res = parser.parse_response(response_buf, backend_response, consumed, false, is_head);
            if (res == http::ParseResult::Complete) {
                int code = static_cast<int>(backend_response.status_code);

                // Handle 101 Switching Protocols
                if (code == 101) {
                    LOG_WARN("101 Switching Protocols received from backend but proxy upgrade tunneling is unsupported: " << backend->address());
                    return http::HttpResponse::make_error(
                        http::StatusCode::BadGateway,
                        "101 Switching Protocols is unsupported by reverse proxy"
                    );
                }

                // Handle interim informational responses (100 Continue, 102 Processing, 103 Early Hints)
                if (code >= 100 && code < 200) {
                    // Forward interim response to client if client socket is available
                    if (client_sock) {
                        std::string interim_str = backend_response.to_string();
                        if (client_sock->send_all(interim_str.data(), interim_str.size()) <= 0) {
                            LOG_ERROR("Failed to forward informational response (" << code << ") to client.");
                            return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Client disconnected during interim response forwarding");
                        }
                    }

                    // If this was 100 Continue and we are waiting to stream the client body to the backend:
                    if (code == 100 && streaming_body_after_100) {
                        streaming_body_after_100 = false;
                        size_t total_expected = request.content_length();
                        size_t total_sent = 0;

                        // 1. Send any initial body bytes that were already in request.body
                        if (!request.body.empty()) {
                            if (backend_sock.send_all(request.body.data(), request.body.size()) <= 0) {
                                backend->total_failures++;
                                return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Failed to stream initial body to upstream");
                            }
                            total_sent += request.body.size();
                        }

                        // 2. Send any bytes that were buffered in client_read_buf
                        if (client_read_buf && !client_read_buf->empty()) {
                            size_t to_take = std::min(client_read_buf->size(), total_expected - total_sent);
                            if (to_take > 0) {
                                if (backend_sock.send_all(client_read_buf->data(), to_take) <= 0) {
                                    backend->total_failures++;
                                    return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Failed to stream buffered body to upstream");
                                }
                                total_sent += to_take;
                                client_read_buf->erase(0, to_take);
                            }
                        }

                        // 3. Read remaining body from client_sock and stream directly to backend_sock
                        char body_chunk[4096];
                        while (total_sent < total_expected) {
                            size_t to_read = std::min(sizeof(body_chunk), total_expected - total_sent);
                            ssize_t n = client_sock->receive(body_chunk, to_read);
                            if (n <= 0) {
                                LOG_ERROR("Client disconnected or timed out while streaming request body after 100 Continue.");
                                return http::HttpResponse::make_error(http::StatusCode::BadRequest, "Client closed connection while transmitting body");
                            }
                            if (backend_sock.send_all(body_chunk, static_cast<size_t>(n)) <= 0) {
                                backend->total_failures++;
                                LOG_ERROR("Failed to send request body chunk to backend.");
                                return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Upstream write error during body transmission");
                            }
                            total_sent += static_cast<size_t>(n);
                        }
                    }

                    response_buf.erase(0, consumed);
                    parser.reset();
                    continue; // Re-check if final response is already present in buffer
                }

                // Final response (>= 200) received!
                return backend_response;
            } else if (res == http::ParseResult::Error) {
                backend->total_failures++;
                LOG_ERROR("Malformed response received from backend: " << backend->address() << " - " << parser.get_error_message());
                return http::HttpResponse::make_error(parser.get_error_status(), parser.get_error_message());
            } else {
                // Incomplete: need more bytes from backend
                break;
            }
        }
    }

    // Only if genuine EOF occurred, attempt final parse for close-delimited response
    if (genuine_eof) {
        while (!response_buf.empty()) {
            size_t consumed = 0;
            http::ParseResult res = parser.parse_response(response_buf, backend_response, consumed, true, is_head);
            if (res == http::ParseResult::Complete) {
                int code = static_cast<int>(backend_response.status_code);
                if (code >= 100 && code < 200) {
                    response_buf.erase(0, consumed);
                    parser.reset();
                    continue;
                }
                return backend_response;
            }
            break;
        }
    }

    backend->total_failures++;
    LOG_ERROR("Truncated or incomplete response from backend: " << backend->address());
    return http::HttpResponse::make_error(http::StatusCode::BadGateway, "Incomplete response from upstream server");
}

} // namespace proxy
