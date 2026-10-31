# Threat Model (STRIDE Methodology)

## 1. Scope
The threat model evaluates the attack surface of the reverse proxy exposed to external clients and downstream microservices.

---

## 2. STRIDE Assessment

| Threat Category | Potential Attack Vector | Applied Mitigation | Status |
| :--- | :--- | :--- | :--- |
| **Spoofing** | Attacker impersonates legitimate client or backend | Injects `X-Forwarded-For` with client IP address; upstream validation. | Mitigated |
| **Tampering** | HTTP request smuggling via dual or conflicting headers | Rejects conflicting `Content-Length` headers; rejects unsupported `Transfer-Encoding: chunked`. | Mitigated |
| **Repudiation** | Client denies issuing requests | High-resolution structured logging with timestamps, client IP, method, and URI. | Mitigated |
| **Information Disclosure** | Memory leaks exposing previous request buffers | RAII buffer wiping, standard string boundaries, ASan zero-leak verification. | Mitigated |
| **Denial of Service (DoS)** | Slowloris slow-drip attack; memory exhaustion | Socket read/write timeouts (`SO_RCVTIMEO`); hard limits on header size (8 KB) and body size (10 MB); bounded thread pool queue with backpressure. | Mitigated |
| **Elevation of Privilege** | Remote code execution via buffer overflow | Pure modern C++20 standard containers with bounds validation; zero unchecked pointer arithmetic; zero raw memory copies. | Mitigated |
