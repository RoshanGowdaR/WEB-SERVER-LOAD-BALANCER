# PERFORMANCE LIMITATIONS & BOTTLENECK ANALYSIS
## High-Performance C++20 HTTP Reverse Proxy & Load Balancer

**Document Version:** 1.1  
**Last Updated:** 2026-11-02  
**Target Audience:** SDE Interviewers, Systems Architects, Infrastructure Engineers

---

## 1. Concurrency Architecture & Scalability Envelope

The current server uses a classic **Worker Thread Pool** model:
- Incoming TCP connections are accepted on a dedicated thread using `::poll()`.
- Accepted connections are dispatched into a bounded queue processed by `N` worker threads (default `N = 4` or `hardware_concurrency`).
- Each worker handles the connection synchronously until completion or timeout.

### Scalability Limits
- **Optimal Concurrency:** 10 to 500 concurrent active connections.
- **Queue Capacity:** Configurable (default 1024). When the queue saturates, backpressure triggers immediate rejection with HTTP 503 Service Unavailable to protect memory.
- **Connection Model:** Thread-per-active-request. Unlike event-driven architectures (`epoll` reactor / Linux `io_uring`), this architecture is bounded by OS thread stack memory and context-switching overhead under 10,000+ simultaneous connections (the C10K problem).

---

## 2. Upstream Connection Lifecycle (No Connection Pooling)

- Each forwarded request establishes a fresh TCP connection to the selected backend using `Connection: close`.
- **Trade-off:**
  - *Advantage:* Complete isolation between client requests, zero risk of HTTP pipelining desynchronization or stale socket reuse bugs.
  - *Limitation:* Incurs a 3-way TCP handshake (`SYN -> SYN-ACK -> ACK`) and connection teardown per backend request.
  - *Impact:* Under high request rates, ephemeral port exhaustion (`TIME_WAIT` socket buildup) can occur if `tcp_tw_reuse` is disabled on the host.

---

## 3. Buffer Allocation & Memory Consumption

- HTTP request headers and response headers are buffered in contiguous `std::string` buffers up to `max_header_size` (default 8 KB).
- Request and response bodies are buffered up to `max_body_size` (default 10 MB).
- **Limitation:** Streaming large file uploads (e.g. 500 MB) is intentionally rejected by `PayloadTooLarge` (HTTP 413) to protect proxy memory buffers from denial-of-service.

---

## 4. Latency Percentile Methodology

- Metrics tracks request latencies in a sliding window reservoir (`LATENCY_WINDOW = 10,000` samples).
- Percentiles (P95, P99) are computed by copying and sorting the reservoir upon request (`/metrics`).
- **Limitation:** Acquires a `std::mutex` during percentile calculation. While fast (< 1 ms for 10k items), extremely high query frequency on `/metrics` under max load introduces minor lock contention.

---

## 5. TLS Termination Absence

- The proxy handles unencrypted HTTP/1.1 traffic.
- In production, it is intended to sit behind an edge TLS terminating reverse proxy (e.g., Cloudflare, AWS ALB, Envoy, or Nginx) or operate within an internal service mesh VPC.

---

## 6. Interview Defense Summary

When discussing performance trade-offs in an interview:
1. **Why thread pool instead of epoll?** Thread pools provide clear, deterministic code readability, clean stack traces, and complete memory isolation. It demonstrates deep knowledge of modern C++ concurrency primitives (`std::condition_variable`, `std::packaged_task`, `std::atomic`) without hundreds of lines of complex epoll state machine boilerplate.
2. **How to scale to 100k connections?** Transition the TCP server layer to Linux `io_uring` or `epoll` with non-blocking socket state machines, while retaining the existing load balancing algorithms and metrics core.
