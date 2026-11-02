# Protocol & Edge Case Verification Report

**Date:** 2026-11-02  
**Components Covered:** `TcpServer`, `ReverseProxy`, `HttpParser`, `Socket`  
**RFC Specifications Audited:** RFC 9110, RFC 9112  

---

## 1. Executive Summary

This report documents the protocol edge cases, security mitigations, and compliance mechanisms implemented and validated during the Final Acceptance Audit.

---

## 2. Protocol Edge Cases & Remediation

### 2.1 Slowloris Defense via Absolute Monotonic Clock Deadline
- **Problem Statement:** Standard socket receive timeouts (`SO_RCVTIMEO`) reset on *every* successful read. An attacker sending 1 byte every $T - 1$ seconds can keep a server thread occupied indefinitely without ever triggering `SO_RCVTIMEO`.
- **Remediation (`src/server/tcp_server.cpp`):**
  - When a client connection begins, an absolute deadline is computed using `std::chrono::steady_clock`:
    `auto request_deadline = std::chrono::steady_clock::now() + config_.connection_timeout;`
  - On each loop iteration, the remaining time until the deadline is calculated.
  - The socket `set_receive_timeout()` is dynamically clamped to `remaining`. If `now >= request_deadline`, the server sends an immediate `408 Request Timeout` with `Connection: close` and severs the connection.
  - Reset occurs only after a complete, valid HTTP request has been received and processed.
- **Verification:** Validated by `ConcurrencyTest.SlowlorisAbsoluteDeadlineEnforced`, where a trickling client was timed out and severed precisely at the configured 300 ms deadline.

### 2.2 HTTP Informational (1xx) Response Handling
- **Problem Statement:** Upstream HTTP/1.1 backends may legitimately send interim 1xx informational responses (such as `100 Continue`, `102 Processing`, or `103 Early Hints`) prior to the final response (status code $\ge 200$). Prematurely returning or failing on a 1xx response breaks client-server interactions.
- **Remediation (`src/proxy/reverse_proxy.cpp` & `src/http/http_parser.cpp`):**
  - Updated `http::StatusCode` enum to support `Continue (100)`, `SwitchingProtocols (101)`, `Processing (102)`, and `EarlyHints (103)`.
  - Updated `parse_response` to correctly handle responses with zero headers (e.g. `HTTP/1.1 100 Continue\r\n\r\n`).
  - In `ReverseProxy::forward_to_backend()`, when an interim response ($100 \le \text{status} < 200$) is parsed, its bytes are consumed and erased from the response buffer, the parser is reset, and reading continues from the upstream socket until the final $\ge 200$ response arrives.
  - For `101 Switching Protocols`, because bidirectional tunnel hijacking (e.g. WebSocket upgrading) is outside current scope, the proxy cleanly logs a warning and returns `502 Bad Gateway` ("101 Switching Protocols is unsupported by reverse proxy").
- **Verification:** Validated by `HttpParserTest.ParseBackendResponseInformational1xx`, `ReverseProxyTest.HandlesInterim100ContinueResponse`, and `ReverseProxyTest.Rejects101SwitchingProtocolsWithBadGateway`.

### 2.3 Transfer-Encoding & Proxy Framing Validation
- **Problem Statement:** HTTP request smuggling occurs when upstream proxies and backends disagree on message boundaries (e.g. chunked transfer encoding vs Content-Length).
- **Remediation:**
  - Client requests with conflicting `Transfer-Encoding` and `Content-Length` are rejected with `400 Bad Request`.
  - Client requests with chunked `Transfer-Encoding` are rejected with `501 Not Implemented`.
  - Upstream backend responses with `Transfer-Encoding: chunked` (or other non-identity encodings) are rejected with `502 Bad Gateway` ("Chunked or unsupported Transfer-Encoding from backend is not supported; Content-Length framing required").
  - Duplicate `Content-Length` headers are rejected with `400 Bad Request` (client) or `502 Bad Gateway` (backend), even if values are identical.
- **Verification:** Validated by `SecurityTest.CL_TE_Desync_Rejected`, `SecurityTest.DuplicateContentLength_SameValue_Rejected`, and `HttpParserTest.RejectBackendChunkedTransferEncoding`.

### 2.4 Multi-Threaded Socket Descriptor Lifecycle
- **Problem Statement:** TSan identified a potential data race where `stop()` closes the listening socket descriptor while an accept thread is reading it in `get_fd()`.
- **Remediation (`src/network/socket.cpp`):**
  - Socket descriptor modernized to `std::atomic<int> fd_{-1}`.
  - `close()` uses `fd_.exchange(-1, std::memory_order_acq_rel)`, guaranteeing that only one thread can execute `::close()` and completely preventing double-close or descriptor races.
- **Verification:** Validated under ThreadSanitizer with zero warnings across all 6 test suites.
