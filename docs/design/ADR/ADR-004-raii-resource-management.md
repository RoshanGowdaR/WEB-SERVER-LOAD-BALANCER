# ADR-004: Strict RAII for System Resources and Active Connection Tracking

## Status
Accepted

## Context
In high-throughput networked services, resource leaks (open file descriptors, orphaned threads, un-decremented connection counters) degrade system stability and lead to socket exhaustion (`EMFILE`).

## Decision
Enforce strict RAII across all system resources:
1. `net::Socket`: Manages file descriptor lifetime with deleted copy constructor and move-only semantics. Destructor invokes `::close(fd_)`.
2. `balancer::BackendConnectionGuard`: Increments active connections upon creation and decrements on destruction, guaranteeing accurate metrics even in exception or early-return code paths.

## Consequences
- **Positive:** Complete prevention of file descriptor and connection count leaks; verified by AddressSanitizer tests with zero memory or resource leaks.
- **Negative:** Requires disciplined move semantics and shared pointer management when transferring socket ownership into multi-threaded callables.
