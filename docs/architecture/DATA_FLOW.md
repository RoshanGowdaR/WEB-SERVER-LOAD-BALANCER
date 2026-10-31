# Data Flow & Request Lifecycle

This document describes the step-by-step end-to-end lifecycle of an HTTP request traversing the load balancer.

```mermaid
sequenceDiagram
    autonumber
    actor Client
    participant OS as OS / Kernel
    participant Acceptor as TcpServer Accept Loop
    participant Pool as ThreadPool
    participant Worker as Worker Thread
    participant Proxy as ReverseProxy
    participant LB as ILoadBalancer
    participant Backend as Upstream Backend

    Client->>OS: TCP SYN (Port 8080)
    OS-->>Client: TCP SYN-ACK
    Client->>OS: TCP ACK (3-Way Handshake Complete)
    Acceptor->>OS: accept()
    OS-->>Acceptor: client_fd (peer IP & Port)
    Acceptor->>Pool: enqueue(connection_task)
    Pool->>Worker: Wake worker via cv_task_available
    Worker->>Client: recv(buffer)
    Client->>Worker: HTTP Request Bytes
    Worker->>Worker: HttpParser::parse_request()
    Worker->>Proxy: handle_request(HttpRequest, client_ip)
    
    alt Request path is /health, /metrics, or /backends
        Proxy->>Worker: Synthetic HTTP Response (200 OK)
    else Forwarding to Backend
        Proxy->>LB: select_backend(request)
        LB-->>Proxy: Chosen BackendServer (e.g. node-1)
        Proxy->>Backend: TCP Connect & Send HTTP Request (with X-Forwarded-For)
        Backend-->>Proxy: Backend HTTP Response Bytes
        Proxy->>Worker: HttpResponse
    end

    Worker->>Client: send_all(HttpResponse)
    Worker->>Worker: Check Connection Keep-Alive
    alt Connection: close
        Worker->>Client: TCP FIN (Close socket)
    end
```

---

## Detailed Step Walkthrough

1. **TCP Handshake:** Client establishes a standard TCP 3-way handshake with the server's listening socket.
2. **Acceptance:** `TcpServer`'s accept loop wakes up via `::poll()`, extracts the client socket, and wraps it into an RAII handle.
3. **Task Dispatch:** The connection task is submitted to `ThreadPool`. If the queue is saturated, a `503 Service Unavailable` with `Retry-After: 5` is returned immediately.
4. **Parsing:** The worker thread executes an incremental streaming read loop using `HttpParser`. Headers are parsed and validated against size limits.
5. **Routing / Interception:**
   - Administrative endpoints (`/health`, `/metrics`, `/backends`) are handled directly in-memory without contacting upstreams.
   - Normal requests query `ILoadBalancer`.
6. **Upstream Forwarding:** The proxy establishes an outbound connection to the chosen backend, appends proxy headers (`X-Forwarded-For`, `Via`, `X-Forwarded-Proto`), and streams the request.
7. **Response Relaying:** The backend response is parsed, verified, and sent back to the client.
8. **Telemetry Recording:** Latency duration and status are recorded in thread-safe atomic counters in `monitoring::Metrics`.
