# Project Explanation Handbook (Elevator Pitches & Technical Defense)

## 1. 30-Second Elevator Pitch
"I built a high-performance multithreaded HTTP reverse proxy and load balancer in C++20 using Linux POSIX sockets. It accepts concurrent client connections, runs an incremental streaming HTTP parser, and routes requests across upstream backends using Round Robin and Least Connections algorithms. It features a bounded ThreadPool with backpressure to eliminate crash risks under load spikes, background health checking with hysteresis recovery, and native Prometheus metrics exposition."

---

## 2. 1-Minute Pitch
"In this project, I engineered a production-quality C++20 HTTP reverse proxy and load balancer from scratch without external web or networking frameworks. 
Architecturally, it separates the accept loop from request processing using a bounded thread pool to prevent thread exhaustion. The networking layer wraps POSIX socket primitives in strict move-only RAII abstractions that completely eliminate resource leaks. 
To guarantee reliability, an active background health checker continuously polls upstreams and uses hysteresis state transitions to prevent flapping. 
I also built an RFC 9112 streaming HTTP parser with protections against HTTP request smuggling and Slowloris attacks. 
The system has been verified under AddressSanitizer, mutation fuzzing, and ApacheBench load testing, achieving over 2,100 requests per second with sub-10ms tail latency."

---

## 3. 3-Minute Comprehensive Technical Walkthrough
"When designing this reverse proxy and load balancer, my primary objective was to demonstrate fundamental systems engineering, low-level networking, and concurrent programming.

The application is structured into modular layers:
1. **Network Engine:** Built directly on POSIX system calls (`socket`, `bind`, `listen`, `accept`, `poll`). I used RAII to wrap file descriptors so sockets close deterministically. Socket options like `SO_REUSEADDR`, `SO_REUSEPORT`, and `TCP_NODELAY` are explicitly tuned, and `SIGPIPE` is masked via `MSG_NOSIGNAL` to prevent process termination on client disconnects.
2. **Streaming HTTP Engine:** Instead of pulling in a massive external framework, I implemented an RFC 9112 compliant state-machine parser. It handles incremental TCP packets across socket boundaries, enforces strict header and payload boundaries to eliminate memory exhaustion attacks, and rejects ambiguous framing headers to guard against HTTP request smuggling.
3. **Concurrency & ThreadPool:** To handle traffic spikes without unbounded thread spawning, I implemented a ThreadPool with a bounded task queue and condition variable synchronization. When the queue saturates, the server applies an explicit backpressure policy, responding with `503 Service Unavailable` with `Retry-After: 5` rather than crashing with out-of-memory errors.
4. **Load Balancing & Active Health Checking:** I designed an `ILoadBalancer` interface with implementations for Round Robin and Least Connections. A separate background worker thread actively monitors backend health using TCP/HTTP probes with hysteresis counters—requiring multiple consecutive failures before taking a node offline, and multiple successes before bringing it back.
5. **Observability & Testing:** The server exposes native Prometheus metrics on `/metrics` and JSON status on `/health` and `/backends`. I built an extensive test suite with GoogleTest, CTest, libFuzzer mutation testing, and an automated Python end-to-end verification script. In benchmarks, it sustained over 2,100 requests per second with zero errors and zero memory leaks under AddressSanitizer."
