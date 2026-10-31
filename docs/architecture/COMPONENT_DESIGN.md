# Component Design

This document details the responsibilities, interfaces, and design patterns utilized in each software component.

---

## 1. Network Subsystem

### `net::Socket`
- **Pattern:** RAII (Resource Acquisition Is Initialization), Move-Only Semantics.
- **Responsibilities:**
  - Wraps a Linux socket file descriptor (`int fd_`).
  - Releases the file descriptor automatically on destruction via `::close(fd_)`.
  - Prohibits copying to eliminate double-free errors.
  - Implements `send_all()` and `receive_all()` loops handling partial writes and `EINTR` signals.

### `net::ServerSocket`
- Extends `net::Socket`.
- Configures `SO_REUSEADDR` and `SO_REUSEPORT` for rapid restart without `TIME_WAIT` conflicts.
- Invokes `getsockname()` after binding to resolve kernel-assigned ephemeral ports.
- Accepts incoming client connections and extracts peer IP and port.

### `net::ClientSocket`
- Extends `net::Socket`.
- Implements non-blocking connection with timeout using `::poll()` on `POLLOUT` and `getsockopt(SOL_SOCKET, SO_ERROR)`.

---

## 2. HTTP Subsystem

### `http::HttpParser`
- **Pattern:** Finite State Machine (FSM), Incremental Streaming.
- **Responsibilities:**
  - Parses raw byte streams into structured `HttpRequest` and `HttpResponse` objects.
  - Returns `ParseResult::Incomplete` when full delimiters (`\r\n\r\n`) or full `Content-Length` bodies have not yet arrived.
  - Returns `ParseResult::Error` with HTTP status codes (e.g., 400, 413, 431, 501) on violation of protocol constraints.

### `http::HttpRequest` & `http::HttpResponse`
- Container objects for method, URI, headers, status codes, and bodies.
- Headers are stored in `std::map<std::string, std::string, CaseInsensitiveCompare>` to guarantee case-insensitive header lookup per RFC 9110.
- Implements `to_string()` for network serialization.

---

## 3. Concurrency Subsystem

### `threading::ThreadPool`
- **Pattern:** Producer-Consumer with Bounded Buffer.
- **Components:**
  - `std::queue<std::function<void()>> tasks_`: Guarded by `std::mutex`.
  - `std::condition_variable cv_task_available_`: Signals idle worker threads.
  - `std::condition_variable cv_space_available_`: Notifies producers when queue capacity becomes available.
  - `BackpressurePolicy`: Either `Reject` (immediate failure) or `BlockWithTimeout`.

---

## 4. Load Balancing Subsystem

### `balancer::ILoadBalancer`
- Pure virtual interface defining `select_backend()`, `add_backend()`, and `get_backends()`.

### `balancer::RoundRobinBalancer`
- Atomic round-robin index: `index_.fetch_add(1, std::memory_order_relaxed) % count`.
- Filters backends based on `is_healthy.load(std::memory_order_relaxed)`.

### `balancer::LeastConnectionsBalancer`
- Iterates over healthy backends and selects the instance with minimum `active_connections`.
- Implements an atomic tie-breaker offset to distribute requests uniformly among backends with identical load.

### `balancer::BackendConnectionGuard`
- RAII guard that increments `active_connections` upon instantiation and automatically decrements on destruction.
