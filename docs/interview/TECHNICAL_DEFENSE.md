# Technical Defense Guide

This document equips the developer to defend design choices, address limitations, and discuss architectural trade-offs during a technical interview.

---

## 1. Defending Technology Choices

### Why C++20 instead of C++11 or C++14?
- **Defense:** C++20 introduces essential language primitives that significantly enhance safety and performance: `std::string_view` enables zero-copy HTTP parsing, `<chrono>` calendar and duration enhancements allow precise timeout tracking, and modern atomic memory models allow lock-free metrics collection without memory fences.

### Why direct POSIX sockets instead of Boost.Asio or Libuv?
- **Defense:** Boost.Asio is an outstanding production library, but incorporating it would obscure the core networking fundamentals this project is designed to master and demonstrate. Using direct POSIX calls (`socket`, `bind`, `listen`, `accept`, `setsockopt`) proves a deep, working comprehension of kernel transport control blocks, TCP flags, and low-level socket lifecycles.

### Why Thread Pool instead of Thread-Per-Connection?
- **Defense:** Thread-per-connection scales poorly beyond hundreds of connections due to thread stack memory allocations (8MB default in Linux) and kernel scheduler context-switching thrashing. A fixed-size ThreadPool with bounded queues bounds resource consumption, ensures deterministic tail latency, and protects the server against denial-of-service crashes.

---

## 2. Acknowledging Limitations Professionally

When asked: *"What are the limitations of your current design and what would you improve in v2?"*
- **Answer:**
  1. **Event-Driven Reactor (`epoll` / `io_uring`):** While our bounded thread pool handles 2,000+ RPS easily, scaling to 100,000 idle persistent connections would benefit from migrating the accept loop and socket reads to an event-driven `epoll` reactor loop or Linux `io_uring` zero-copy I/O.
  2. **TLS Termination:** The current proxy handles cleartext HTTP/1.1. In enterprise environments, TLS termination would be integrated using OpenSSL or BoringSSL.
  3. **HTTP/2 & HTTP/3:** Adding HTTP/2 multiplexing (binary framing, header compression via HPACK) would improve throughput on high-latency client connections.
