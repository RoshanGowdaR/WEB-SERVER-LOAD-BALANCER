#pragma once

#include <string>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

namespace net {

/**
 * @brief RAII wrapper for POSIX socket file descriptor.
 * Guarantees safe cleanup, no fd leakage, and non-copyable semantics.
 */
class Socket {
public:
    Socket();
    explicit Socket(int fd);
    ~Socket();

    // Move-only semantics (RAII)
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    [[nodiscard]] int get_fd() const noexcept { return fd_; }
    [[nodiscard]] bool is_valid() const noexcept { return fd_ >= 0; }

    void close() noexcept;
    void shutdown(int how = SHUT_RDWR) noexcept;

    // Socket configuration
    bool set_reuse_address(bool enable = true) noexcept;
    bool set_reuse_port(bool enable = true) noexcept;
    bool set_non_blocking(bool enable = true) noexcept;
    bool set_tcp_nodelay(bool enable = true) noexcept;
    bool set_receive_timeout(std::chrono::milliseconds timeout) noexcept;
    bool set_send_timeout(std::chrono::milliseconds timeout) noexcept;

    // Core I/O primitives with robust partial read/write loops
    ssize_t send(const void* data, size_t length, int flags = MSG_NOSIGNAL) noexcept;
    ssize_t send_all(const void* data, size_t length) noexcept;
    ssize_t send_string(std::string_view data) noexcept;

    ssize_t receive(void* buffer, size_t max_length, int flags = 0) noexcept;
    ssize_t receive_all(void* buffer, size_t length) noexcept;

protected:
    int fd_{-1};
};

/**
 * @brief Server-side listening socket.
 */
class ServerSocket : public Socket {
public:
    ServerSocket() = default;

    bool bind_and_listen(const std::string& host, uint16_t port, int backlog = 1024) noexcept;
    [[nodiscard]] Socket accept(std::string* client_ip = nullptr, uint16_t* client_port = nullptr) noexcept;

    [[nodiscard]] std::string get_bound_ip() const noexcept { return bound_ip_; }
    [[nodiscard]] uint16_t get_bound_port() const noexcept { return bound_port_; }

private:
    std::string bound_ip_;
    uint16_t bound_port_{0};
};

/**
 * @brief Client-side connecting socket (for proxy to backend).
 */
class ClientSocket : public Socket {
public:
    ClientSocket() = default;

    bool connect(const std::string& host, uint16_t port, std::chrono::milliseconds timeout) noexcept;
};

} // namespace net
