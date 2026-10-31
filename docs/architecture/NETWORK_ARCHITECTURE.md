# Network Architecture

## 1. Network Protocol Stack

```
+-------------------------------------------------------------+
| Application Layer: HTTP/1.1 (RFC 9110, RFC 9112)           |
+-------------------------------------------------------------+
| Transport Layer: TCP (RFC 793, RFC 7323)                   |
| - Streaming Byte-Oriented Communication                     |
| - Flow Control (TCP Window) & Congestion Control (CUBIC/BBR)|
+-------------------------------------------------------------+
| Internet Layer: IPv4 (RFC 791) / POSIX Sockets (AF_INET)   |
+-------------------------------------------------------------+
```

---

## 2. Low-Level Socket Tuning & POSIX Calls

1. **`SO_REUSEADDR` & `SO_REUSEPORT`**:
   - `SO_REUSEADDR` enables rapid binding to a port in the `TIME_WAIT` state during server restart.
   - `SO_REUSEPORT` allows the operating system kernel to distribute incoming connections across multiple listening sockets.
2. **`TCP_NODELAY`**:
   - Disables Nagle's algorithm (`setsockopt(IPPROTO_TCP, TCP_NODELAY, &opt)`).
   - Reduces latency for small HTTP request/response payloads by transmitting packets immediately without waiting for ACKs.
3. **`SO_RCVTIMEO` & `SO_SNDTIMEO`**:
   - Enforces socket-level deadlines to protect against Slowloris attacks (slow-drip header transmissions) and hanging upstream backends.
4. **`MSG_NOSIGNAL` & Signal Handling**:
   - In UNIX systems, writing to a TCP socket closed by the remote peer triggers a kernel `SIGPIPE` signal, which terminates the process by default.
   - The server calls `signal(SIGPIPE, SIG_IGN)` at initialization and passes `MSG_NOSIGNAL` flag on all `send()` operations.
