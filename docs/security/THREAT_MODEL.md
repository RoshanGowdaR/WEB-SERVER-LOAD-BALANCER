# THREAT MODEL & SECURITY ARCHITECTURE (STRIDE)
## High-Performance C++20 HTTP Reverse Proxy & Load Balancer

**Document Version:** 2.0 (Remediated)  
**Last Updated:** 2026-11-02  
**Framework:** Microsoft STRIDE Threat Modeling Methodology  
**Review Status:** Verified with Automated Security Test Suite

---

## 1. System Scope & Trust Boundaries

The reverse proxy operates at the boundary between untrusted external clients and internal upstream backend servers.

```
+--------------------+        Untrusted Network (Plain HTTP/1.1)
|  External Clients  | ------------------------------------+
+--------------------+                                     |
                                                           v
+--------------------------------------------------------------------------+
|  C++ Load Balancer & Reverse Proxy Boundary                              |
|                                                                          |
|  - TCP Socket Filter (Timeouts, SO_REUSEADDR, TCP_NODELAY)               |
|  - Strict HTTP Parser (RFC 9110 Token Grammar, std::from_chars)          |
|  - Hop-by-Hop Header Sanitization (RFC 9110 Section 7.6.1)               |
|  - Trusted X-Forwarded-For Insertion                                     |
|  - Bounded ThreadPool Backpressure (HTTP 503 Reject)                     |
+--------------------------------------------------------------------------+
                                                           |
                                                           v  Trusted VPC Network
                                              +--------------------------+
                                              | Upstream Backend Servers |
                                              +--------------------------+
```

---

## 2. STRIDE Assessment Matrix

| Threat Category | Attack Vector | Technical Impact | Applied Mitigation | Verification Evidence |
|---|---|---|---|---|
| **Spoofing** | Client sends forged `X-Forwarded-For: 8.8.8.8` | Backend misattributes client identity | Proxy strips client-supplied `X-Forwarded-For` and sets true socket IP | `ReverseProxyTest.ReplacesClientSuppliedXForwardedForToPreventSpoofing` |
| **Spoofing** | Host Header Poisoning (multiple `Host` headers) | Web cache poisoning, routing bypass | Enforce strictly one `Host` header for HTTP/1.1; reject duplicates | `SecurityTest.HostHeaderPoisoning_MultipleHostHeaders` |
| **Tampering** | HTTP Request Smuggling (CL.TE / TE.CL conflict) | Pipeline hijacking, request desync | Rejects requests with both `Content-Length` and `Transfer-Encoding` with HTTP 400 | `SecurityTest.RequestSmuggling_ClTeConflict` |
| **Tampering** | Duplicate `Content-Length` headers | Request smuggling across upstream proxy | Rejects all duplicate `Content-Length` headers regardless of casing | `SecurityTest.RequestSmuggling_DuplicateContentLength` |
| **Tampering** | CRLF Injection in header names or values | Response splitting, cache poisoning | Strict RFC 9110 token grammar on field names; lone CR/LF validation on values | `SecurityTest.HeaderInjection_EmbeddedCRLFInValue` |
| **Repudiation** | Client denies transaction | Auditing failure | Structured logging with microsecond timestamps, client IP, method, and URI | `LoggerTest`, `Server::handle_client` logging |
| **Information Disclosure** | Buffer over-read or uninitialized memory | Memory leakage across requests | Modern C++ `std::string` and `std::string_view` bounds; ASan verification | Valgrind / ASan report: 0 leaks |
| **Information Disclosure** | Internal backend network topology leak | Attackers discover internal IP addresses | Observability endpoints `/health`, `/metrics`, `/backends` document topology risk | Documented in `docs/api/API_REFERENCE.md` |
| **Denial of Service** | Slowloris slow header attack | Worker exhaustion, connection starvation | Connection deadlines (`SO_RCVTIMEO` and `SO_SNDTIMEO`) set on accept | `ServerConcurrencyTest`, socket timeouts |
| **Denial of Service** | Oversized header / body memory exhaustion | Out-of-memory process crash | Hard bounds on `max_header_size` (8 KB) and `max_body_size` (10 MB) | `SecurityTest.ResourceExhaustion_OversizedHeader` |
| **Denial of Service** | Task queue saturation flood | Thread pool memory growth | Configurable bounded queue with backpressure policy returning HTTP 503 | `ConcurrencyTest.ThreadPoolBackpressureUnderHeavyLoad` |
| **Elevation of Privilege** | Memory corruption (stack/heap buffer overflow) | Remote code execution | Move-only RAII sockets, bounds checking, zero raw pointer arithmetic | ASan + UBSan 100% clean |

---

## 3. Residual Risk & Architectural Limitations

1. **Unencrypted HTTP (Lack of TLS Termination):** Traffic between clients and proxy is unencrypted plain HTTP. Production deployments must place the proxy behind a TLS terminator (e.g. AWS ALB, Cloudflare, or Nginx).
2. **Administrative Endpoint Protection:** Internal endpoints (`/metrics`, `/health`, `/backends`) are unauthenticated. In enterprise deployments, access should be restricted via firewall rules or reverse proxy path filtering.
