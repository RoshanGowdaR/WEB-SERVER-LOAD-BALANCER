# Design Decisions Summary

| Topic | Chosen Approach | Alternative Considered | Rationale |
| :--- | :--- | :--- | :--- |
| **Language Standard** | C++20 | C++11 / C++17 | Concepts, `<chrono>` calendar improvements, `std::string_view`, standard atomic enhancements. |
| **Networking Primitives** | Direct Linux POSIX Sockets | Boost.Asio / libuv | Native socket programming demonstrates fundamental systems engineering and OS networking concepts without framework obscuration. |
| **Concurrency Architecture** | Fixed Bounded ThreadPool | Thread-per-request | Thread-per-request causes OS thread exhaustion, context switching overhead, and susceptibility to DoS. |
| **Queue Backpressure** | Explicit Reject / Timeout | Unbounded Queue | Unbounded queues cause runaway memory consumption and high tail latencies during load spikes. |
| **Resource Management** | RAII wrappers (`net::Socket`, `BackendConnectionGuard`) | Manual fd close and manual counter tracking | Guarantees resource release upon stack unwinding, exceptions, or early returns. |
| **Header Parsing** | Custom RFC 9112 Streaming FSM | External HTTP parser library | Zero third-party runtime dependencies, strict boundary checks, rejection of smuggling attempts. |
| **Metrics Exposition** | Prometheus Text + JSON | Proprietary binary format | Universal compatibility with standard Grafana / Prometheus / Kubernetes observability ecosystems. |
