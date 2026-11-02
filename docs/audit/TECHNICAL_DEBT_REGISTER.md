# TECHNICAL DEBT REGISTER
## High-Performance C++20 HTTP Reverse Proxy & Load Balancer

**Maintained by:** Senior Systems Engineering Team  
**Last Updated:** 2026-11-02  
**Branch:** `developer`

---

## 1. Technical Debt Overview

This register tracks all known architectural trade-offs, deferred features, performance limitations, and technical debt items identified during the engineering audit.

---

## 2. Technical Debt Registry

| Debt ID | Subsystem | Severity | Description | Justification / Impact | Target Milestone |
|---|---|---|---|---|---|
| **DEBT-001** | Network / TLS | Medium | Absence of native TLS (HTTPS) termination | Proxy operates in plain HTTP/1.1; designed for private network deployment behind an edge TLS terminator (e.g. Envoy/Cloudflare). | Post-v1.0 (OpenSSL integration) |
| **DEBT-002** | HTTP Parser | Medium | Bounded buffer parsing rather than zero-copy streaming state machine | Current implementation searches delimiters in contiguous buffer. Clean and simple to reason about, but requires buffering header sections. | Architecture v2.0 |
| **DEBT-003** | Proxy Forwarding | Low | No HTTP/1.1 backend connection pooling (keep-alive to backends) | Backends use `Connection: close` per forwarded request. Simplifies connection lifecycle and avoid desync bugs, but adds TCP handshake overhead to upstreams. | v1.1 Connection Pool |
| **DEBT-004** | Health Checking | Low | Health checker uses active polling rather than passive out-of-band monitoring | Backends are polled at fixed intervals; active traffic failures increment failure counts, but dynamic circuit-breaking could be enhanced. | v1.1 Circuit Breaker |
| **DEBT-005** | Load Balancing | Low | Absence of IP hash / sticky sessions | Only Round Robin and Least Connections are supported; stateful web sessions require sticky cookie hashing. | v1.2 Sticky Sessions |
| **DEBT-006** | Configuration | Low | Regex-based JSON parser rather than dedicated parser library | Project deliberately avoids heavy external dependencies; JSON parsing is lightweight and tailored for config format. | Retained for minimal footprint |
| **DEBT-007** | Metrics | Low | Sliding window reservoir for latency percentiles requires mutex lock | Sufficient for current throughput, but high-concurrency lock-free histograms (e.g. HdrHistogram) would scale better under extreme loads. | v2.0 Lock-free Metrics |
| **DEBT-008** | Event Loop | Medium | Thread-per-connection worker pool rather than non-blocking `io_uring` or `epoll` reactor | Classic thread pool architecture is robust, highly understandable for interviews, and portable POSIX, but scales to thousands rather than millions of concurrent idle connections. | Documented Architecture Choice |

---

## 3. Maintenance & Deprecation Policy

Each technical debt item is evaluated against project goals:
1. **Defensibility:** Can the current architectural decision be clearly explained and defended in a systems programming interview?
2. **Correctness:** Does the debt compromise correctness or security under the supported operational envelope?
3. **Complexity Budget:** Does resolving the debt introduce dependencies or complexity that detracts from codebase clarity?
