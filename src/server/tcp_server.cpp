#include "server/tcp_server.h"
#include "http/http_parser.h"
#include "monitoring/metrics.h"
#include "logging/logger.h"
#include <csignal>
#include <poll.h>

namespace server {

TcpServer::TcpServer(
    config::ServerConfig config,
    std::shared_ptr<threading::ThreadPool> thread_pool,
    std::shared_ptr<proxy::ReverseProxy> proxy
) : config_(std::move(config)),
    thread_pool_(std::move(thread_pool)),
    proxy_(std::move(proxy)) {
    // Ignore SIGPIPE globally to avoid process termination on broken client sockets
    std::signal(SIGPIPE, SIG_IGN);
}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::start() {
    if (running_.load()) return true;

    if (!listening_socket_.bind_and_listen(config_.host, config_.port, 2048)) {
        LOG_ERROR("Failed to bind and listen on " << config_.host << ":" << config_.port);
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread(&TcpServer::accept_loop, this);
    LOG_INFO("TcpServer listening on http://" << config_.host << ":" << config_.port);
    return true;
}

void TcpServer::stop() {
    if (!running_.exchange(false)) return;

    LOG_INFO("Stopping TcpServer...");
    listening_socket_.close();

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    LOG_INFO("TcpServer stopped.");
}

void TcpServer::accept_loop() {
    while (running_.load()) {
        struct pollfd pfd{};
        pfd.fd = listening_socket_.get_fd();
        pfd.events = POLLIN;

        int poll_res = ::poll(&pfd, 1, 500); // 500ms timeout for clean cancellation
        if (poll_res < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (poll_res == 0) {
            continue; // Poll timeout, re-check running flag
        }

        std::string client_ip;
        uint16_t client_port = 0;
        net::Socket client_sock = listening_socket_.accept(&client_ip, &client_port);

        if (!client_sock.is_valid()) {
            if (!running_.load()) break;
            continue;
        }

        // Delegate client handling to thread pool
        if (thread_pool_) {
            auto sock_ptr = std::make_shared<net::Socket>(std::move(client_sock));
            std::string log_ip = client_ip;
            uint16_t log_port = client_port;
            auto task = [this, sock_ptr, ip = std::move(client_ip), port = client_port]() mutable {
                handle_client(std::move(*sock_ptr), std::move(ip), port);
            };

            bool enqueued = thread_pool_->enqueue(std::move(task));
            if (!enqueued) {
                // Backpressure rejection: task queue is saturated
                monitoring::Metrics::instance().record_rejected_request();
                LOG_WARN("ThreadPool overloaded: rejecting client connection from " << log_ip << ":" << log_port);
                auto reject_resp = http::HttpResponse::make_error(
                    http::StatusCode::ServiceUnavailable,
                    "Server is overloaded. Please retry later."
                );
                reject_resp.set_header("Retry-After", "5");
                std::string s = reject_resp.to_string();
                sock_ptr->send_all(s.data(), s.size());
                sock_ptr->close();
            }
        } else {
            handle_client(std::move(client_sock), std::move(client_ip), client_port);
        }
    }
}

void TcpServer::handle_client(net::Socket client_sock, std::string client_ip, uint16_t client_port) {
    (void)client_port;
    client_sock.set_receive_timeout(config_.connection_timeout);
    client_sock.set_send_timeout(config_.connection_timeout);
    client_sock.set_tcp_nodelay(true);

    http::HttpParser parser(config_.max_header_size, config_.max_body_size);
    std::string read_buffer;
    char buffer[4096];

    while (running_.load()) {
        ssize_t bytes = client_sock.receive(buffer, sizeof(buffer));
        if (bytes <= 0) {
            // Client closed connection or timeout reached
            break;
        }

        read_buffer.append(buffer, static_cast<size_t>(bytes));

        while (!read_buffer.empty()) {
            http::HttpRequest request;
            size_t bytes_consumed = 0;
            http::ParseResult res = parser.parse_request(read_buffer, request, bytes_consumed);

            if (res == http::ParseResult::Complete) {
                // Remove parsed request from buffer
                read_buffer.erase(0, bytes_consumed);

                // Process request through reverse proxy
                http::HttpResponse response;
                if (proxy_) {
                    response = proxy_->handle_request(request, client_ip);
                } else {
                    response = http::HttpResponse::make_error(http::StatusCode::InternalServerError, "Proxy unconfigured");
                }

                // If client or response requests close, honor it
                bool keep_alive = request.is_keep_alive() && response.is_keep_alive();
                if (!keep_alive) {
                    response.set_header("Connection", "close");
                } else {
                    response.set_header("Connection", "keep-alive");
                }

                std::string resp_str = response.to_string();
                if (client_sock.send_all(resp_str.data(), resp_str.size()) <= 0) {
                    return; // Broken pipe or client disconnect
                }

                if (!keep_alive) {
                    return;
                }

                parser.reset();
            } else if (res == http::ParseResult::Error) {
                http::HttpResponse err_resp = http::HttpResponse::make_error(
                    parser.get_error_status(),
                    parser.get_error_message()
                );
                std::string s = err_resp.to_string();
                client_sock.send_all(s.data(), s.size());
                return;
            } else {
                // ParseResult::Incomplete: Need more data from client
                break;
            }
        }
    }
}

} // namespace server
