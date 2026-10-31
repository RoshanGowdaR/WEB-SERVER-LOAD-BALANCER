#include "network/connection.h"
#include <utility>

namespace net {

Connection::Connection(Socket socket, std::string peer_ip, uint16_t peer_port)
    : socket_(std::move(socket)),
      peer_ip_(std::move(peer_ip)),
      peer_port_(peer_port) {}

ssize_t Connection::read(void* buffer, size_t max_len) noexcept {
    return socket_.receive(buffer, max_len);
}

ssize_t Connection::write(const void* data, size_t len) noexcept {
    return socket_.send_all(data, len);
}

ssize_t Connection::write_string(std::string_view data) noexcept {
    return socket_.send_string(data);
}

} // namespace net
