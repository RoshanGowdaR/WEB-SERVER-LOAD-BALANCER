# Known Issues & Limitations

As of the latest engineering remediation (v1.1.0-remediated), all P0/P1 defects discovered during development and the engineering audit have been resolved and verified via regression and security test suites.

## 1. Resolved Historical Bugs
- `BUG-20261031-01` (Fixed & Verified) - Ephemeral port zero binding
- `BUG-20261031-02` (Fixed & Verified) - Move-only socket lambda capture
- `BUG-20261031-03` (Fixed & Verified) - Thread pool worker handshake race
- `BUG-20261031-04` (Fixed & Verified) - Close-delimited response parsing EOF flag

See [BUG_FIX_HISTORY.md](BUG_FIX_HISTORY.md) for full root cause analyses.

## 2. Engineering Limitations & Architectural Scope
- **HTTP/1.1 Plaintext:** TLS (HTTPS) termination is not implemented natively. Production deployment requires an edge TLS proxy (e.g. Envoy, Nginx, or AWS ALB).
- **Backend Keep-Alive:** The proxy connects to upstreams with `Connection: close` to maintain clean connection lifecycles without connection pooling complexity.
- **Bounded Buffering:** Header sections are bounded to 8 KB and bodies to 10 MB to prevent resource exhaustion.
- **Worker Thread Model:** Scalable up to hundreds of concurrent active connections; not designed for millions of idle connections (C10K problem).
