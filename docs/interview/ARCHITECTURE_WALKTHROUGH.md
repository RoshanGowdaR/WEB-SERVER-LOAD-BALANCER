# Architecture Walkthrough for Technical Interviews

## Key Interview Talking Points

1. **Why decouple the Accept Loop from Worker Threads?**
   - The accept loop (`TcpServer::accept_loop`) has a single responsibility: calling `::poll()` and `accept()` on the listening socket. If the accept loop also parsed HTTP or established upstream TCP connections, any slow client or high-latency backend would block all other pending connections in the kernel backlog queue. Offloading accepted sockets immediately to worker threads maximizes connection acceptance rate.

2. **How does the system prevent memory exhaustion during traffic surges?**
   - Traditional servers with unbounded task queues or thread-per-connection models suffer memory exhaustion (OOM kills) when backends slow down. Our `ThreadPool` enforces a bounded task queue (`max_queue_size`). When saturated, it applies backpressure by rejecting new tasks and returning `HTTP 503 Service Unavailable`, protecting both host memory and running worker threads.

3. **How is HTTP Request Smuggling prevented?**
   - HTTP request smuggling typically occurs when frontends and backends disagree on message framing (e.g. dual `Content-Length` headers, or combining `Transfer-Encoding: chunked` with `Content-Length`). Our `HttpParser` explicitly detects conflicting `Content-Length` values and rejects them with `400 Bad Request`. Additionally, downstream chunked requests are rejected with `501 Not Implemented`.

4. **Why is Hysteresis critical in Health Checking?**
   - Without hysteresis, a backend experiencing a single dropped packet or transient 100ms GC pause would immediately be marked unhealthy, causing rapid flapping of traffic back and forth. By requiring $N=3$ consecutive failures before marking down, and $M=2$ consecutive successes before marking healthy, system stability is preserved.
