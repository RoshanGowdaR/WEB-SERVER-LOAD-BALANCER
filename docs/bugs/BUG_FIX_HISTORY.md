# Bug Fix History & Engineering Remediation Log

This document records confirmed defects discovered during the design, implementation, and verification phases of the High-Performance C++ Load Balancer, along with root causes, fixes, and regression test references.

---

### BUG-20261031-01: Ephemeral Port Query Failure in ServerSocket Bind

- **Bug ID:** BUG-20261031-01
- **Title:** `ServerSocket::get_bound_port()` returns 0 when bound to ephemeral port
- **Severity:** High
- **Priority:** P1
- **Affected Component:** `src/network/socket.cpp` (`ServerSocket::bind_and_listen`)
- **Environment:** Linux / WSL2, GCC 13.3
- **Description:** When binding to port 0 (ephemeral OS-assigned port) in unit and integration tests, `bound_port_` remained set to 0 rather than retrieving the actual kernel-assigned port.
- **Root Cause:** After `::bind()` and `::listen()` succeeded, `bound_port_ = port;` was assigned directly without invoking `getsockname()` to query the assigned port from the kernel socket structure.
- **Fix Implemented:** Added `::getsockname(fd_, ...)` inspection after successful bind and listen to resolve the network port via `ntohs()`.
- **Regression Test Reference:** `SocketTest.ClientServerCommunication` (`tests/unit/test_socket.cpp`)
- **Status:** Resolved / Verified

---

### BUG-20261031-02: std::function Copy-Constructibility Violation with Move-Only Socket

- **Bug ID:** BUG-20261031-02
- **Title:** Static assertion failure in `ThreadPool::enqueue` when capturing move-only `net::Socket`
- **Severity:** High
- **Priority:** P0
- **Affected Component:** `src/server/tcp_server.cpp`
- **Environment:** C++20, GCC 13.3
- **Description:** Moving `net::Socket` directly into a lambda target for `std::function<void()>` resulted in compilation error `static assertion failed: std::function target must be copy-constructible`.
- **Root Cause:** `std::function` requires its callable target to satisfy `std::is_copy_constructible`. `net::Socket` has a deleted copy constructor by design to preserve strict RAII socket ownership.
- **Fix Implemented:** Wrapped the client socket in `std::shared_ptr<net::Socket>` upon accept, allowing safe copying of the shared handle into the `std::function` task while guaranteeing destruction and socket closure upon task completion.
- **Regression Test Reference:** `ServerConcurrencyTest.HandleMultipleConcurrentClients`
- **Status:** Resolved / Verified

---

### BUG-20261031-03: ThreadPool Enqueue Race Condition in Backpressure Test

- **Bug ID:** BUG-20261031-03
- **Title:** `ThreadPoolTest.BackpressureRejectionPolicy` intermittent failure due to worker dequeue timing
- **Severity:** Medium
- **Priority:** P2
- **Affected Component:** `tests/unit/test_thread_pool.cpp`
- **Environment:** Concurrency test suite
- **Description:** Enqueuing tasks immediately after enqueuing a blocking task occasionally failed because the worker had not yet popped the first task, causing early queue saturation.
- **Root Cause:** Lack of explicit synchronization verifying that the worker thread had actually transitioned from the task queue into execution before the main test thread filled the bounded queue.
- **Fix Implemented:** Added an atomic `worker_started` handshake signal in the blocking task, ensuring the test thread waits until the worker has actively popped the task before testing queue saturation boundaries.
- **Regression Test Reference:** `ThreadPoolTest.BackpressureRejectionPolicy`
- **Status:** Resolved / Verified

---

### BUG-20261031-04: Premature Completion of Response Parsing without Content-Length

- **Bug ID:** BUG-20261031-04
- **Title:** `HttpParser::parse_response` returning `Complete` prematurely on responses without `Content-Length`
- **Severity:** High
- **Priority:** P1
- **Affected Component:** `src/http/http_parser.cpp` & `src/proxy/reverse_proxy.cpp`
- **Environment:** End-to-End System Integration
- **Description:** When a backend response did not specify `Content-Length`, the parser completed immediately upon seeing `\r\n\r\n`, truncating any subsequently transmitted body data.
- **Root Cause:** According to RFC 9112 Section 6.3, responses without framing headers (Content-Length / chunked) delimit payload by connection close (EOF). The parser did not differentiate between an in-flight socket read and an EOF event.
- **Fix Implemented:** Extended `HttpParser::parse_response` with an explicit `is_eof` flag. When no `Content-Length` is provided, the parser returns `ParseResult::Incomplete` during the streaming read loop, and only transitions to `ParseResult::Complete` when `bytes_read == 0` (EOF).
- **Regression Test Reference:** `scripts/e2e_test.py`
- **Status:** Resolved / Verified
