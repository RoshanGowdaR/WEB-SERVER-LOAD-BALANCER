# Bugs, Limitations & Known Issues

This document outlines known limitations, trade-offs, and intentional architectural boundaries in the current implementation.

---

## 1. Protocol Limitations

1. **HTTP/1.1 Subset**:
   - The server supports `GET`, `POST`, `PUT`, `DELETE`, `HEAD`, and `OPTIONS`.
   - `Transfer-Encoding: chunked` downstream request parsing is intentionally rejected with `501 Not Implemented` to protect against HTTP request smuggling vulnerabilities (RFC 9112 §6.3). Upstream backends are communicated with using `Content-Length` framing or connection close.
2. **WebSocket & HTTP/2**:
   - WebSockets (RFC 6455) and HTTP/2 multiplexing are not supported in this version.
3. **TLS/SSL Encryption**:
   - This implementation handles plain HTTP/1.1. In standard production enterprise topologies, TLS termination is performed at the boundary (e.g. AWS ALB, Cloudflare, or edge termination) before routing to internal load balancers.

---

## 2. Operating System & I/O Model Boundaries

1. **Worker-per-Connection Thread Pool Model**:
   - The server utilizes POSIX sockets managed by an asynchronous task queue backed by a fixed `ThreadPool`.
   - For ultra-high concurrency (> 50,000 idle persistent connections), an event-driven reactor loop (`epoll` edge-triggered or `io_uring`) is recommended over blocking worker dispatch. For the target concurrency envelope (1,000–5,000 concurrent active connections), the current thread-pool architecture achieves lower tail latency and zero event loop starvation.
2. **Platform Dependency**:
   - The networking engine relies on POSIX socket APIs (`sys/socket.h`, `netinet/in.h`, `poll.h`). It runs on Linux, macOS, and WSL2. Native Windows Winsock is not targeted directly.

---

## 3. Security Considerations & Hardening

1. **Default Loopback Binding**:
   - The default configuration binds to `127.0.0.1` to prevent inadvertent public exposure without firewall policies.
2. **Buffer Limits**:
   - Maximum header size is hard-capped at 8 KB (`max_header_size`).
   - Maximum body size is capped at 10 MB (`max_body_size`).
   - Both prevent memory exhaustion attacks.
