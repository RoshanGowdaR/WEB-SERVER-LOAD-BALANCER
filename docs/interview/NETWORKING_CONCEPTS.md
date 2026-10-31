# Networking Concepts Applied in This Project

1. **TCP Sockets & POSIX Lifecycle:**
   - Server lifecycle: `socket()` creates endpoint $\rightarrow$ `setsockopt()` tunes options $\rightarrow$ `bind()` assigns local address/port $\rightarrow$ `listen()` sets incoming queue backlog $\rightarrow$ `accept()` extracts established connection from kernel TCP handshake queue.
   - Client lifecycle: `socket()` $\rightarrow$ non-blocking `connect()` with `::poll()` timeout $\rightarrow$ `send()` / `recv()`.

2. **TCP 3-Way Handshake & Backlog:**
   - The kernel manages SYN queue and accept backlog. We configure `listen(fd_, 2048)` to accommodate connection bursts during high load.

3. **Nagle's Algorithm & TCP_NODELAY:**
   - By default, Nagle's algorithm buffers small outgoing TCP packets until prior packet ACKs arrive. Setting `TCP_NODELAY` disables this buffering, significantly decreasing HTTP latency for small headers and payloads.

4. **Handling Partial Sends and Receives:**
   - TCP is a byte-stream protocol without message boundaries. `send()` and `recv()` can process fewer bytes than requested. Our `send_all()` and `receive_all()` loops continually advance buffer pointers until completion or error.

5. **SIGPIPE & Process Protection:**
   - Writing to a closed TCP socket causes the kernel to send `SIGPIPE`, killing the process. We mask it process-wide with `signal(SIGPIPE, SIG_IGN)` and pass `MSG_NOSIGNAL` on every send call.
