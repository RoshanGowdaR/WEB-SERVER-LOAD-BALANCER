# Comprehensive Systems Engineering Interview Question Bank (65+ Questions & Answers)

This question bank is curated for Senior Software Engineer, Systems Engineer, and Backend/Infrastructure roles. Every question is mapped to real-world code and engineering decisions in this project.

---

## Topic 1: Modern C++ & Memory Management (Questions 1–12)

### Q1: What is RAII and how is it used in this project?
**Answer:** RAII (Resource Acquisition Is Initialization) ties the lifetime of system resources to the lifetime of stack objects. In `net::Socket`, the socket descriptor is initialized in the constructor and released via `::close(fd_)` in the destructor. Even if an exception is thrown or a function returns early, stack unwinding guarantees that the socket descriptor is closed without leakage.

### Q2: Why are copy constructors deleted for `net::Socket`?
**Answer:** Sockets wrap unique operating system file descriptors. If copying were allowed, multiple `Socket` instances would hold the same integer descriptor. When one instance went out of scope, its destructor would close the descriptor, leaving the other instance with a dangling, invalid file descriptor, and triggering a double-close bug. Making it move-only guarantees exclusive ownership.

### Q3: What is the purpose of `noexcept` in move operations?
**Answer:** Marking move constructors and move assignment operators `noexcept` informs STL containers (such as `std::vector`) that moving elements will never throw. When `std::vector` reallocates its internal buffer, it will move elements instead of copying them. If move operations are not `noexcept`, `std::vector` falls back to copying to maintain the strong exception guarantee.

### Q4: Why did capturing `net::Socket` directly in `std::function` fail compilation?
**Answer:** `std::function` requires its callable target to be copy-constructible. Because `net::Socket` is move-only, a lambda capturing `sock = std::move(client_sock)` is not copy-constructible. In C++20, wrapping the socket in `std::shared_ptr<net::Socket>` provides copyable semantics for the lambda while preserving deterministic cleanup upon task completion.

### Q5: What is `std::string_view` and what are its performance advantages?
**Answer:** `std::string_view` is a non-owning reference to a contiguous sequence of characters, comprising a pointer and a length. In `HttpParser`, using `std::string_view` allows slicing request lines, headers, and tokens without allocating heap memory or copying characters.

### Q6: What is a dangling `string_view` risk and how did we avoid it?
**Answer:** Since `std::string_view` does not own memory, if the underlying string buffer is modified or destroyed, the `string_view` points to invalid memory. In `HttpParser::parse_request`, parsed strings stored long-term in `HttpRequest` (such as `uri` and header strings) are explicitly copied into `std::string` objects to guarantee independent ownership.

### Q7: Why use `std::jthread` or `std::thread`?
**Answer:** `std::thread` represents an OS execution thread. In C++20, `std::jthread` automatically requests cancellation via `std::stop_token` and joins on destruction. In this project, threads are managed deterministically with explicit join loops during graceful shutdown.

### Q8: What is `std::atomic` and how does `memory_order_relaxed` differ from `memory_order_seq_cst`?
**Answer:** `std::atomic` provides atomic read-modify-write operations without mutex locking. `memory_order_seq_cst` guarantees a globally consistent execution order across all threads (the default, with a slight memory fence overhead). `memory_order_relaxed` guarantees atomicity of the single variable without imposing ordering constraints on other variables, which is optimal for independent counters like `total_requests_` and `index_`.

### Q9: How does `compare_exchange_weak` work in `Metrics::record_request_end`?
**Answer:** In `Metrics`, calculating `max_latency_us_` uses `compare_exchange_weak` in a lock-free loop:
```cpp
uint64_t cur_max = max_latency_us_.load(std::memory_order_relaxed);
while (lat > cur_max && !max_latency_us_.compare_exchange_weak(cur_max, lat, std::memory_order_relaxed)) {}
```
If another thread updates `max_latency_us_` concurrently, `compare_exchange_weak` updates `cur_max` with the new value and retries without taking a mutex lock.

### Q10: What is the Rule of Five in C++?
**Answer:** If a class manages a resource and defines a destructor, it should explicitly declare or delete all five special member functions: Destructor, Copy Constructor, Copy Assignment, Move Constructor, and Move Assignment. `net::Socket` explicitly deletes copy operations and declares move operations and destructor.

### Q11: What is AddressSanitizer (ASan) and how does it detect bugs?
**Answer:** ASan instruments memory accesses by surrounding allocated heap blocks with "redzones" and tracking a shadow memory map. Any out-of-bounds access or dereference of freed memory hits a redzone, triggering an immediate crash with a diagnostic stack trace.

### Q12: Why avoid `using namespace std;` in header files?
**Answer:** Placing `using namespace std;` in a header pollutes the global namespace of every compilation unit that includes it, creating potential name collisions (e.g. `std::bind` vs socket `::bind()`).

---

## Topic 2: Computer Networking & POSIX Sockets (Questions 13–24)

### Q13: Explain the POSIX socket server lifecycle.
**Answer:**
1. `socket()`: Allocates a socket descriptor in the kernel.
2. `setsockopt()`: Sets options like `SO_REUSEADDR` and `TCP_NODELAY`.
3. `bind()`: Associates the socket with a local IP address and port.
4. `listen()`: Puts socket into passive mode and sets the connection backlog.
5. `accept()`: Blocks or polls until a completed 3-way handshake connection is available in the queue, returning a new connected socket descriptor.

### Q14: What is the difference between the listening socket and connected socket?
**Answer:** The listening socket (`ServerSocket`) only listens on a specific port and accepts incoming connections. When `accept()` succeeds, the kernel creates a new connected socket (`Socket`) with its own unique file descriptor specifically assigned to that client's four-tuple (source IP, source port, dest IP, dest port).

### Q15: Why is `SO_REUSEADDR` essential?
**Answer:** When a server restarts, sockets previously in communication remain in the `TIME_WAIT` state for 1–2 minutes (2MSL). Without `SO_REUSEADDR`, `bind()` fails with `EADDRINUSE` (Address already in use). `SO_REUSEADDR` permits the socket to bind to the port immediately.

### Q16: Why is `TCP_NODELAY` enabled on client and backend sockets?
**Answer:** By default, Nagle's algorithm coalesces small packets to save network bandwidth, delaying transmissions until prior packets are acknowledged. For HTTP request/response traffic, this introduces artificial 40ms–200ms latency delays. Setting `TCP_NODELAY` disables Nagle's algorithm.

### Q17: What causes a `SIGPIPE` signal and how is it handled?
**Answer:** When writing to a socket that has been closed by the remote peer (RST received), the kernel sends a `SIGPIPE` signal. The default handler terminates the process. We ignore `SIGPIPE` via `signal(SIGPIPE, SIG_IGN)` and pass `MSG_NOSIGNAL` to `send()`, causing `send()` to return -1 with `errno = EPIPE` instead.

### Q18: What is the purpose of `::getsockname()`?
**Answer:** When binding to port 0 (ephemeral port), the kernel assigns an arbitrary free port. `getsockname()` retrieves the locally bound address and port from the socket file descriptor, enabling the application to discover the allocated port.

### Q19: Why must network byte order (`htons`, `ntohs`) be used?
**Answer:** Network protocols transmit integers in Big-Endian format (most significant byte first). x86_64 host architectures use Little-Endian. `htons()` (Host TO Network Short) and `ntohs()` (Network TO Host Short) ensure correct port representation across architectures.

### Q20: How do `send_all()` and `receive_all()` handle partial writes and `EINTR`?
**Answer:** TCP is a byte-stream protocol; `send()` can write fewer bytes than requested if kernel buffers are full. `send_all()` loops until all bytes are sent, advancing the buffer pointer. If interrupted by a signal, `errno == EINTR`, the loop continues rather than returning an error.

### Q21: What is the difference between TCP and UDP?
**Answer:** TCP is connection-oriented, reliable, ordered, and flow-controlled with a 3-way handshake and ACK-based retransmissions. UDP is connectionless, unreliable, unordered, and packet-oriented with lower header overhead. HTTP/1.1 uses TCP.

### Q22: What happens during a TCP 3-way handshake?
**Answer:**
1. Client sends `SYN` (Synchronize with sequence number $X$).
2. Server responds with `SYN-ACK` (Sequence number $Y$, ACK $X+1$).
3. Client sends `ACK` (ACK $Y+1$). Connection is now established.

### Q23: What is the `TIME_WAIT` state?
**Answer:** `TIME_WAIT` is entered by the endpoint that initiates the active close (`FIN`). It lasts for 2 times the Maximum Segment Lifetime (2MSL, typically 60s) to ensure the remote peer receives the final ACK and to allow delayed packets in the network to expire before the same port tuple is reused.

### Q24: How does `::poll()` work in the accept loop?
**Answer:** `::poll()` monitors file descriptors for events (`POLLIN` for readable data or pending connections). Specifying a 500ms timeout allows the loop to unblock periodically, check if a shutdown has been requested, and exit cleanly without hanging.

---

## Topic 3: HTTP Protocol & Security (Questions 25–36)

### Q25: How does HTTP/1.1 framing work?
**Answer:** HTTP/1.1 requests start with a Request Line (`METHOD URI VERSION\r\n`), followed by zero or more Header lines (`Name: Value\r\n`), an empty line delimiter (`\r\n`), and an optional Body whose length is dictated by `Content-Length` or `Transfer-Encoding`.

### Q26: What is HTTP Request Smuggling and how does this server prevent it?
**Answer:** Request smuggling occurs when discrepancies in frontend and backend HTTP parsers (e.g. regarding `Content-Length` vs `Transfer-Encoding`) cause one server to treat a single request as two. Our server explicitly rejects requests with conflicting or duplicate `Content-Length` headers (`400 Bad Request`) and rejects downstream `chunked` requests (`501 Not Implemented`).

### Q27: How does the server defend against Slowloris attacks?
**Answer:** Slowloris sends HTTP headers very slowly (e.g. 1 byte every 10 seconds) to tie up server threads. The server configures `SO_RCVTIMEO` (default 5 seconds). If a client fails to transmit data within this timeout, the socket read fails and the connection is closed.

### Q28: What is the difference between HTTP/1.0 and HTTP/1.1 connection handling?
**Answer:** In HTTP/1.0, connections close by default after each response unless `Connection: keep-alive` is specified. In HTTP/1.1, connections remain open by default (keep-alive) unless `Connection: close` is specified.

### Q29: What is the function of the `X-Forwarded-For` header?
**Answer:** When a reverse proxy forwards a request, the backend sees the proxy's IP address as the TCP peer. The proxy appends the original client IP to the `X-Forwarded-For` header so upstream backends can identify the true origin IP.

### Q30: What is the `Via` header used for?
**Answer:** The `Via` header tracks the sequence of proxies through which a request or response passes (e.g. `Via: 1.1 cpp-load-balancer`), helping detect proxy loops and tracking request lineage.

### Q31: What is the difference between 502 Bad Gateway and 504 Gateway Timeout?
**Answer:** `502 Bad Gateway` indicates the proxy received an invalid response, connection reset, or connection refusal when trying to reach the upstream backend. `504 Gateway Timeout` indicates the proxy connected to the upstream, but the upstream failed to respond within the configured deadline (`backend_timeout_ms`).

### Q32: What is the difference between 503 Service Unavailable and 502 Bad Gateway?
**Answer:** `502` represents a communication failure with a selected upstream. `503` indicates the load balancer itself cannot handle the request at this time—either because all backends are marked unhealthy, or because the internal thread pool queue is full.

### Q33: How are HTTP header lookups made case-insensitive?
**Answer:** RFC 9110 specifies that HTTP header field names are case-insensitive. `http::HttpRequest` uses `std::map<std::string, std::string, CaseInsensitiveCompare>`, where the custom comparator lowercases characters before comparison.

### Q34: How does the parser handle pipelined or segmented requests?
**Answer:** Sockets receive data in arbitrary byte chunks. `HttpParser::parse_request` takes a `data` buffer and an output parameter `bytes_consumed`. If only part of a request has arrived, it returns `ParseResult::Incomplete`. When complete, `bytes_consumed` is erased from the front of the read buffer, leaving any pipelined second request for subsequent parsing.

### Q35: What happens when an HTTP request header exceeds 8 KB?
**Answer:** The parser rejects the request immediately with `HTTP 431 Request Header Fields Too Large` and aborts further reading from the socket, protecting server memory against buffer bloating.

### Q36: What is idempotent HTTP method behavior?
**Answer:** An idempotent method produces the same server state regardless of whether it is executed once or multiple times (`GET`, `HEAD`, `PUT`, `DELETE`). `POST` is non-idempotent. Proxies can safely retry idempotent requests on transient failure, but must avoid blindly retrying non-idempotent `POST` requests.

---

## Topic 4: Concurrency, Threading & Synchronization (Questions 37–48)

### Q37: What is the Producer-Consumer problem and how does `ThreadPool` solve it?
**Answer:** The producer creates tasks; consumers process them. If unsynchronized, race conditions occur. `ThreadPool` guards the shared task queue with `std::mutex`. When the queue is empty, workers wait on `cv_task_available_.wait(lock)`. When a task is enqueued, `notify_one()` wakes a worker without busy-waiting.

### Q38: What is spurious wakeup in condition variables?
**Answer:** An OS thread waiting on a condition variable can wake up even if no notification was sent. To prevent processing an empty queue, condition variables must always be waited on inside a predicate loop:
```cpp
cv.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
```

### Q39: Why is a bounded task queue superior to an unbounded queue?
**Answer:** Under heavy load spikes or backend outages, an unbounded queue will accumulate millions of pending tasks, consuming gigabytes of heap memory until the process is terminated by the OS Out-Of-Memory (OOM) killer. A bounded queue enforces a hard ceiling, shedding load with `503 Service Unavailable` to preserve system stability.

### Q40: What is a deadlock and what four Coffman conditions are required?
**Answer:** A deadlock is a state where two or more threads are permanently blocked waiting for locks held by each other. The four Coffman conditions are:
1. Mutual Exclusion.
2. Hold and Wait.
3. No Preemption.
4. Circular Wait.
In this project, deadlocks are prevented by avoiding nested lock acquisitions across subsystems.

### Q41: What is a Data Race?
**Answer:** A data race occurs when two concurrent threads access the same memory location without synchronization, and at least one access is a write. Data races invoke Undefined Behavior in C++. We eliminate data races using atomics and mutexes, verified by ThreadSanitizer.

### Q42: What is the difference between `std::mutex` and `std::shared_mutex`?
**Answer:** `std::mutex` is exclusive: only one thread can hold the lock at any time. `std::shared_mutex` (reader-writer lock) allows multiple threads to acquire shared read access (`std::shared_lock`) concurrently, while requiring exclusive access (`std::unique_lock`) for writes. This is used in `RoundRobinBalancer` for the backend list.

### Q43: How does the thread pool handle exceptions thrown inside a task?
**Answer:** If an exception escapes a thread's entry function, `std::terminate()` is called, crashing the entire process. In `ThreadPool::worker_loop()`, each task invocation is enclosed in `try { task(); } catch (const std::exception& ex) { ... } catch (...) { ... }`. The exception is logged, and the worker survives to process the next task.

### Q44: What is Context Switching overhead?
**Answer:** A context switch occurs when the OS scheduler preempts a running thread and loads the CPU registers and memory mappings of another thread. Excessive context switching (e.g. creating thousands of threads) burns CPU cycles in kernel scheduling rather than application logic. Fixed thread pools prevent this overhead.

### Q45: How is graceful shutdown coordinated across threads?
**Answer:** An atomic `stop_` flag is set to `true`, and `notify_all()` is called on all condition variables. Worker threads wake up, see `stop_ == true` and empty queues, exit their loops, and are joined using `worker.join()` in the main thread.

### Q46: What is lock contention and how can it be minimized?
**Answer:** Lock contention occurs when multiple threads compete for the same mutex, forcing threads into kernel sleep. It is minimized by keeping critical sections short, avoiding I/O inside mutex locks, and using lock-free atomics for counters.

### Q47: What is thread affinity?
**Answer:** Thread affinity binds a thread to a specific CPU core (`pthread_setaffinity_np`), improving CPU L1/L2 cache hit rates by preventing the OS scheduler from migrating threads between cores.

### Q48: Why use condition variable `notify_one()` vs `notify_all()`?
**Answer:** `notify_one()` wakes a single waiting thread, preventing the "thundering herd" problem where all threads wake up and contend for the mutex when only one task is available. `notify_all()` is reserved for shutdown events where all workers must terminate.

---

## Topic 5: System Design & Load Balancing (Questions 49–58)

### Q49: What is the difference between a Reverse Proxy and a Forward Proxy?
**Answer:** A forward proxy acts on behalf of clients, mediating requests outbound to the internet (often for caching, anonymity, or firewall bypass). A reverse proxy acts on behalf of servers, sitting at the network perimeter to receive client requests and distribute them to private upstream backends.

### Q50: How does the Round Robin algorithm work?
**Answer:** Round Robin cycles through a list of healthy backends sequentially. Using an atomic counter:
$$\text{selected\_index} = (\text{counter}++) \pmod N$$
If the selected backend is marked unhealthy, the algorithm inspects the next node in sequence.

### Q51: How does the Least Connections algorithm work?
**Answer:** Least Connections scans all healthy backends and selects the instance with the minimum number of active concurrent connections. It is well-suited for requests with varying processing times (e.g. database-heavy queries vs fast static responses).

### Q52: What is the tie-breaker mechanism in our Least Connections implementation?
**Answer:** When multiple backends have the exact same minimum connection count (e.g. all 0), scanning from index 0 every time would bias traffic to the first node. Our implementation uses an atomic offset to tie-break across nodes uniformly.

### Q53: What is Health Check Flapping and how does Hysteresis prevent it?
**Answer:** Flapping is rapid cycling between healthy and unhealthy states due to transient network spikes. Hysteresis establishes different thresholds for state transitions: requiring 3 consecutive probe failures to transition to unhealthy, and 2 consecutive successes to transition back to healthy.

### Q54: What happens when ALL backends fail health checks?
**Answer:** The load balancer logs a critical warning, excludes all dead nodes, and returns `HTTP 503 Service Unavailable` with `Retry-After: 5` to clients. This prevents requests from hanging in connection timeout loops.

### Q55: Active vs Passive Health Checking?
**Answer:** Active health checking uses dedicated probe requests (e.g. `GET /health` every 5 seconds) sent by a background thread. Passive health checking monitors real client traffic, marking nodes unhealthy if actual requests return errors. Our implementation uses active health checking.

### Q56: What is Horizontal Scaling vs Vertical Scaling?
**Answer:** Vertical scaling adds more CPU/RAM to an existing machine. Horizontal scaling adds more machines/nodes behind a load balancer. Load balancers are the cornerstone of horizontal scaling.

### Q57: Why export metrics in Prometheus format?
**Answer:** Prometheus text exposition is an industry standard (OpenMetrics). Metrics can be scraped periodically by Prometheus and visualized in Grafana dashboards with zero proprietary adapter code.

### Q58: What is the difference between P95 and P99 latency?
**Answer:** P95 latency is the threshold under which 95% of requests complete; P99 is the threshold under which 99% complete. P99 reflects tail latency experienced by the slowest 1% of users, highlighting micro-stalls, lock contention, or garbage collection pauses.

---

## Topic 6: Operating Systems & System Calls (Questions 59–65+)

### Q59: What is the purpose of `backlog` in `listen(fd, backlog)`?
**Answer:** `backlog` defines the maximum queue length of pending connections that have completed or are completing the TCP 3-way handshake in the kernel before being retrieved by `accept()`. If the queue overflows, incoming client SYNs are dropped or rejected with RST.

### Q60: What is non-blocking I/O (`O_NONBLOCK`)?
**Answer:** By default, socket calls block the calling thread until data is available. Setting `O_NONBLOCK` causes `recv()` or `connect()` to return immediately with `EAGAIN` or `EWOULDBLOCK` if the operation cannot proceed without waiting, allowing single threads to multiplex sockets using `poll()` or `epoll()`.

### Q61: What is the difference between `epoll` and `poll`?
**Answer:** `poll()` has $O(N)$ complexity because the kernel must scan the entire array of descriptors on every call. `epoll` (Linux event poll) has $O(1)$ complexity: descriptors are registered once in a kernel red-black tree, and ready events are returned in a ready-list via callbacks, allowing scaling to 100,000+ idle connections.

### Q62: What is the maximum number of open files per process and how does it limit a server?
**Answer:** Controlled by `RLIMIT_NOFILE` (`ulimit -n`). Each TCP connection consumes one file descriptor. If a server reaches the limit, `accept()` fails with `EMFILE` (Too many open files). In production, this limit is tuned to 65,535 or higher.

### Q63: What is the difference between a process and a thread?
**Answer:** A process is an independent execution unit with its own private virtual memory space, file descriptor table, and page tables. Threads share the virtual address space, heap, and open file descriptors of their parent process, but retain their own private stack and register states.

### Q64: What is CPU cache thrashing in multithreaded systems?
**Answer:** Cache thrashing occurs when threads running on different cores repeatedly read and write to variables located in the same L1/L2 cache line (False Sharing), causing the cache line to bounce between cores and degrading performance.

### Q65: How do you defend your choice of building this in modern C++ vs Go or Rust?
**Answer:** Modern C++20 provides zero-cost abstractions, deterministic destructors without garbage collection pauses, and direct access to native Linux POSIX socket APIs and kernel tuning parameters. While Go uses green threads (goroutines) with a runtime scheduler and garbage collection pauses, C++ gives absolute predictability over memory layout, tail latency, and system calls.
