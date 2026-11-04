# REMEDIATION PLAN & EXECUTION ROADMAP
## High-Performance C++20 HTTP Reverse Proxy & Load Balancer

**Date:** 2026-11-02  
**Target Branch:** `developer`  
**Execution Strategy:** Sequential Dependency Order

---

## 1. Execution Order & Remediation Steps

### Phase 1: Core System & Signal Safety (P0)
- **Goal:** Eliminate undefined behavior from async signal handling.
- **Action:**
  - In `src/main.cpp`, implement safe signal handling via `std::atomic<bool>` or self-pipe / dedicated sigwait control thread.
  - Zero heap allocation, zero logging, zero mutex acquisition inside async signal handlers.
  - Coordinate graceful shutdown sequence: stop listening socket, reject new enqueues, drain queue, stop health checker, join workers, close resources.
- **Verification:** Dedicated graceful shutdown unit and integration tests.

### Phase 2: HTTP Request & Response Parser Remediation (P0)
- **Goal:** Harden HTTP framing and protect against request smuggling and resource exhaustion.
- **Action:**
  - `src/http/http_parser.cpp` & `include/http/http_parser.h`:
    - Strict `Content-Length` parsing using `std::from_chars`. Reject negative values, spaces, trailing junk, signs.
    - Reject all duplicate or conflicting `Content-Length` headers.
    - Token grammar validation for header names (RFC 9110).
    - Enforce mandatory HTTP/1.1 `Host` header.
    - Validate request-line tokens (method, origin-form URI starting with `/`, HTTP version).
    - Strict limits on header count (max 100) and max header size.
    - Response parser: enforce response header and body limits (`max_body_size`).
    - Special status codes without body (HEAD, 1xx, 204, 304).
- **Verification:** Expanded parser unit tests, HTTP smuggling test suite, fragmented delimiter tests.

### Phase 3: Reverse Proxy Buffering & Header Sanitization (P0/P1)
- **Goal:** Prevent unbounded proxy memory growth and enforce RFC hop-by-hop header removal.
- **Action:**
  - `src/proxy/reverse_proxy.cpp`:
    - Check buffer size against `max_body_size` prior to appending backend data.
    - Disambiguate genuine EOF (`recv() == 0`) from socket errors (`recv() < 0`). Never treat socket errors as EOF.
    - Strip all hop-by-hop headers: `Connection`, `Keep-Alive`, `Proxy-Authenticate`, `Proxy-Authorization`, `TE`, `Trailer`, `Transfer-Encoding`, `Upgrade`, and Connection-nominated headers.
    - Set `X-Forwarded-For` with client IP securely without blind appending to untrusted headers.
- **Verification:** Hop-by-hop stripping tests, oversized backend response rejection tests.

### Phase 4: ThreadPool Shutdown & Backpressure Correctness (P1)
- **Goal:** Ensure precise semantics for graceful vs immediate shutdown.
- **Action:**
  - `src/threading/thread_pool.cpp` & `include/threading/thread_pool.h`:
    - Fix inverted `shutdown(wait_for_all_tasks)` logic.
    - Graceful: let workers drain remaining tasks.
    - Immediate: clear pending tasks immediately under lock, then wake and join workers.
- **Verification:** Unit tests verifying task drain vs task discard.

### Phase 5: Configuration & CLI Validation (P1)
- **Goal:** Fail-fast on invalid input and eliminate crash vectors.
- **Action:**
  - `src/config/config.cpp` & `src/main.cpp`:
    - Strict bounds checking on CLI parameters with safe parsing.
    - Fail-fast with clear error messages when configuration file cannot be read or parsed.
- **Verification:** Config validation unit tests with invalid inputs.

### Phase 6: Real Fuzz Testing (P1)
- **Goal:** Replace pseudo-random loop with true LLVM libFuzzer targets.
- **Action:**
  - Implement `LLVMFuzzerTestOneInput` targets in `tests/fuzz/` for request and response parsers.
  - Provide seed corpus and build instructions for Clang/libFuzzer.

### Phase 7: Comprehensive Test Suite & Benchmark Hardening (P1/P2)
- **Goal:** Add security, concurrency, and regression test suites.
- **Action:**
  - Create `tests/security/test_http_smuggling.cpp`.
  - Create `tests/concurrency/test_concurrent_clients.cpp`.
  - Update `benchmarks/run_benchmark.sh` to use multi-threaded mock servers and proper proxy warm-up.

### Phase 8: CI/CD, Documentation & Interview Handbook
- **Goal:** Ensure all documentation matches the implementation.
- **Action:**
  - Update GitHub Actions workflow, Dockerfile non-root user.
  - Expand `docs/interview/INTERVIEW_QUESTIONS.md` to 75+ questions with file/line references.
  - Update architecture, security, and benchmark documents.
  - Create `docs/REMEDIATION_REPORT.md` and update `docs/PROJECT_COMPLETION_REPORT.md`.
