#include "network/socket.h"
#include <cstring>
#include <cerrno>
#include <netinet/tcp.h>
#include <poll.h>
#include <thread>

namespace net {

Socket::OpGuard::OpGuard(const Socket& sock) noexcept : sock_(const_cast<Socket&>(sock)), fd_(-1) {
    if (sock_.closing_.load(std::memory_order_acquire)) {
        return;
    }
    sock_.active_ops_.fetch_add(1, std::memory_order_acquire);
    if (sock_.closing_.load(std::memory_order_acquire)) {
        sock_.active_ops_.fetch_sub(1, std::memory_order_release);
        return;
    }
    int cur_fd = sock_.fd_.load(std::memory_order_acquire);
    if (cur_fd < 0) {
        sock_.active_ops_.fetch_sub(1, std::memory_order_release);
        return;
    }
    fd_ = cur_fd;
}

Socket::OpGuard::~OpGuard() noexcept {
    if (fd_ >= 0) {
        sock_.active_ops_.fetch_sub(1, std::memory_order_release);
    }
}

Socket::Socket() : fd_(-1), active_ops_(0), closing_(false) {}

Socket::Socket(int fd) : fd_(fd), active_ops_(0), closing_(false) {}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept
    : fd_(other.fd_.exchange(-1, std::memory_order_acq_rel)),
      active_ops_(0),
      closing_(false) {}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        fd_.store(other.fd_.exchange(-1, std::memory_order_acq_rel), std::memory_order_release);
        active_ops_.store(0, std::memory_order_release);
        closing_.store(false, std::memory_order_release);
    }
    return *this;
}

void Socket::close() noexcept {
    // 1. Mark closing to reject any new operations
    closing_.store(true, std::memory_order_release);

    // 2. Atomically take ownership of the descriptor
    int old_fd = fd_.exchange(-1, std::memory_order_acq_rel);
    if (old_fd < 0) {
        closing_.store(false, std::memory_order_release);
        return;
    }

    // 3. Unblock any in-flight syscalls cleanly via shutdown without freeing descriptor number yet
    if (active_ops_.load(std::memory_order_acquire) > 0) {
        ::shutdown(old_fd, SHUT_RDWR);
        while (active_ops_.load(std::memory_order_acquire) > 0) {
            std::this_thread::yield();
        }
    }

    // 4. Zero operations are now using old_fd. It is safe to close.
    // The kernel will only reclaim this descriptor number after all operations have exited.
    ::close(old_fd);
    closing_.store(false, std::memory_order_release);
}

void Socket::shutdown(int how) noexcept {
    OpGuard guard(*this);
    if (guard.is_valid()) {
        ::shutdown(guard.get_fd(), how);
    }
}

bool Socket::set_reuse_address(bool enable) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return false;
    int opt = enable ? 1 : 0;
    return ::setsockopt(guard.get_fd(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == 0;
}

bool Socket::set_reuse_port(bool enable) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return false;
#ifdef SO_REUSEPORT
    int opt = enable ? 1 : 0;
    return ::setsockopt(guard.get_fd(), SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == 0;
#else
    (void)enable;
    return true;
#endif
}

bool Socket::set_non_blocking(bool enable) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return false;
    int fd = guard.get_fd();
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    if (enable) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return ::fcntl(fd, F_SETFL, flags) == 0;
}

bool Socket::set_tcp_nodelay(bool enable) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return false;
    int opt = enable ? 1 : 0;
    return ::setsockopt(guard.get_fd(), IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == 0;
}

bool Socket::set_receive_timeout(std::chrono::milliseconds timeout) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return false;
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    return ::setsockopt(guard.get_fd(), SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
}

bool Socket::set_send_timeout(std::chrono::milliseconds timeout) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return false;
    struct timeval tv{};
    tv.tv_sec = static_cast<time_t>(timeout.count() / 1000);
    tv.tv_usec = static_cast<suseconds_t>((timeout.count() % 1000) * 1000);
    return ::setsockopt(guard.get_fd(), SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

ssize_t Socket::send(const void* data, size_t length, int flags) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return -1;
    return ::send(guard.get_fd(), data, length, flags);
}

ssize_t Socket::send_all(const void* data, size_t length) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return -1;
    int fd = guard.get_fd();
    const char* ptr = static_cast<const char*>(data);
    size_t remaining = length;
    while (remaining > 0) {
        ssize_t bytes_sent = ::send(fd, ptr, remaining, MSG_NOSIGNAL);
        if (bytes_sent < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (bytes_sent == 0) break;
        remaining -= static_cast<size_t>(bytes_sent);
        ptr += bytes_sent;
    }
    return static_cast<ssize_t>(length - remaining);
}

ssize_t Socket::send_string(std::string_view data) noexcept {
    return send_all(data.data(), data.size());
}

ssize_t Socket::receive(void* buffer, size_t max_length, int flags) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return -1;
    ssize_t bytes;
    do {
        bytes = ::recv(guard.get_fd(), buffer, max_length, flags);
    } while (bytes < 0 && errno == EINTR);
    return bytes;
}

ssize_t Socket::receive_all(void* buffer, size_t length) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return -1;
    int fd = guard.get_fd();
    char* ptr = static_cast<char*>(buffer);
    size_t total_received = 0;
    while (total_received < length) {
        ssize_t bytes = ::recv(fd, ptr + total_received, length - total_received, 0);
        if (bytes < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (bytes == 0) break; // EOF
        total_received += static_cast<size_t>(bytes);
    }
    return static_cast<ssize_t>(total_received);
}

// ServerSocket implementation
bool ServerSocket::bind_and_listen(const std::string& host, uint16_t port, int backlog) noexcept {
    close();
    int new_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (new_fd < 0) return false;
    fd_.store(new_fd, std::memory_order_release);

    set_reuse_address(true);
    set_reuse_port(true);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            close();
            return false;
        }
    }

    int current_fd = fd_.load(std::memory_order_acquire);
    if (::bind(current_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close();
        return false;
    }

    if (::listen(current_fd, backlog) < 0) {
        close();
        return false;
    }

    struct sockaddr_in bound_addr{};
    socklen_t len = sizeof(bound_addr);
    if (::getsockname(current_fd, reinterpret_cast<struct sockaddr*>(&bound_addr), &len) == 0) {
        bound_port_ = ntohs(bound_addr.sin_port);
    } else {
        bound_port_ = port;
    }
    bound_ip_ = host;
    return true;
}

Socket ServerSocket::accept(std::string* client_ip, uint16_t* client_port) noexcept {
    OpGuard guard(*this);
    if (!guard.is_valid()) return Socket(-1);

    struct sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    int client_fd = -1;

    do {
        client_fd = ::accept(guard.get_fd(), reinterpret_cast<struct sockaddr*>(&client_addr), &client_len);
    } while (client_fd < 0 && errno == EINTR);

    if (client_fd < 0) {
        return Socket(-1);
    }

    if (client_ip != nullptr) {
        char ip_str[INET_ADDRSTRLEN];
        if (::inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str)) != nullptr) {
            *client_ip = ip_str;
        }
    }
    if (client_port != nullptr) {
        *client_port = ntohs(client_addr.sin_port);
    }

    return Socket(client_fd);
}

// ClientSocket implementation
bool ClientSocket::connect(const std::string& host, uint16_t port, std::chrono::milliseconds timeout) noexcept {
    close();
    int new_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (new_fd < 0) return false;
    fd_.store(new_fd, std::memory_order_release);

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
        close();
        return false;
    }

    // Set non-blocking to honor timeout
    if (!set_non_blocking(true)) {
        close();
        return false;
    }

    int current_fd = fd_.load(std::memory_order_acquire);
    int res = ::connect(current_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (res < 0 && errno != EINPROGRESS) {
        close();
        return false;
    }

    if (res != 0) {
        struct pollfd pfd{};
        pfd.fd = current_fd;
        pfd.events = POLLOUT;
        int poll_res = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
        if (poll_res <= 0) {
            // Timeout or error
            close();
            return false;
        }

        int so_error = 0;
        socklen_t len = sizeof(so_error);
        if (::getsockopt(current_fd, SOL_SOCKET, SO_ERROR, &so_error, &len) < 0 || so_error != 0) {
            close();
            return false;
        }
    }

    // Restore blocking mode
    set_non_blocking(false);
    set_tcp_nodelay(true);
    return true;
}

} // namespace net
