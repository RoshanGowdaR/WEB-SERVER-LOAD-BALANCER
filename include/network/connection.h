#pragma once

#include "network/socket.h"
#include <string>
#include <memory>
#include <chrono>

namespace net {

class Connection {
public:
    Connection(Socket socket, std::string peer_ip, uint16_t peer_port);
    ~Connection() = default;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    Connection(Connection&&) noexcept = default;
    Connection& operator=(Connection&&) noexcept = default;

    [[nodiscard]] Socket& get_socket() noexcept { return socket_; }
    [[nodiscard]] const Socket& get_socket() const noexcept { return socket_; }
    [[nodiscard]] const std::string& get_peer_ip() const noexcept { return peer_ip_; }
    [[nodiscard]] uint16_t get_peer_port() const noexcept { return peer_port_; }

    void close() noexcept { socket_.close(); }
    [[nodiscard]] bool is_open() const noexcept { return socket_.is_valid(); }

    ssize_t read(void* buffer, size_t max_len) noexcept;
    ssize_t write(const void* data, size_t len) noexcept;
    ssize_t write_string(std::string_view data) noexcept;

private:
    Socket socket_;
    std::string peer_ip_;
    uint16_t peer_port_{0};
};

} // namespace net
