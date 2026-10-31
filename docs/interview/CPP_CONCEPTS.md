# C++ Concepts Applied in This Project

1. **RAII (Resource Acquisition Is Initialization):**
   - Implemented in `net::Socket` and `balancer::BackendConnectionGuard`. The destructor deterministically releases the OS file descriptor (`::close(fd_)`) or decrements the active connection atomic counter, ensuring zero resource leaks even during stack unwinding on exceptions.

2. **Move Semantics & Deleted Copy Operations:**
   - Sockets represent exclusive operating system handles. `net::Socket` has a deleted copy constructor and deleted copy assignment operator, and defines a noexcept move constructor. This prevents accidental duplicate ownership and double-closing of file descriptors.

3. **Smart Pointers & Memory Management:**
   - `std::unique_ptr` is used for single-owner components (`tcp_server_` inside `Server`).
   - `std::shared_ptr` is used for shared components (`BackendServer`, `ThreadPool`, `ILoadBalancer`) passed across worker threads.
   - Zero raw pointer ownership (`new` / `delete`).

4. **Modern C++20 Standard Features:**
   - `std::string_view` used extensively in the HTTP parser to inspect and slice string tokens without heap allocations.
   - Strongly-typed `enum class` for `Method`, `StatusCode`, `ParseResult`, and `BackpressurePolicy`.
   - Thread-safe atomics (`std::atomic<bool>`, `std::atomic<uint64_t>`, `std::atomic<size_t>`) with explicit memory ordering semantics (`std::memory_order_relaxed`, `compare_exchange_weak`).
