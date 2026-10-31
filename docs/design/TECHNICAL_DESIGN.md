# Technical Design Document

## 1. Context and Problem Statement
Modern microservice architectures require high-performance, fault-tolerant entry points that can terminate client HTTP connections, distribute workloads uniformly across dynamic upstream nodes, handle backend degradation gracefully, and report detailed operational telemetry without introducing significant latency.

This project delivers a native C++20 reverse proxy and load balancer engineered directly on Linux POSIX networking APIs.

---

## 2. Design Goals
1. **Low Latency & High Throughput:** Efficient socket streaming and minimal memory allocations in request routing.
2. **Deterministic Resource Utilization:** Bounded task queues, strict header/body size limits, and predictable thread counts.
3. **Resilience & Fault Tolerance:** Active background health checking, automated failover, and hysteresis recovery.
4. **Safety:** Zero memory leaks, zero data races, protection against `SIGPIPE`, and rejection of malformed or ambiguous HTTP framing.
5. **Observability:** Native Prometheus-compatible metrics export and JSON status APIs.

---

## 3. High-Level Component Interactions

- **Decoupled Accept and Worker Loops:** `TcpServer`'s accept loop does not parse HTTP or handle upstream connections; it immediately offloads accepted sockets to the `ThreadPool`.
- **Stateless Reverse Proxy:** `ReverseProxy` relies on the abstract `ILoadBalancer` to choose upstreams, enabling hot swapping between Round Robin and Least Connections algorithms without code changes.
- **Background Health Supervision:** Upstream health evaluation is isolated to `HealthChecker`, ensuring health probes never block user request paths.
