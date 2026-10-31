# ADR-002: Direct POSIX Socket APIs for Network Layer

## Status
Accepted

## Context
High-level networking libraries (e.g. Boost.Asio, Poco, libevent) provide portable event abstractions, but introduce extensive dependency trees and hide low-level socket lifecycle calls (`socket`, `bind`, `listen`, `accept`, `recv`, `send`, `setsockopt`).

## Decision
Implement the networking layer directly using Linux POSIX socket APIs wrapped in lightweight RAII abstractions.

## Consequences
- **Positive:** Zero external network runtime dependencies; transparent control over socket buffer flags, options, and system calls; ideal for systems engineering rigor and interview evaluation.
- **Negative:** Non-portable to non-POSIX systems (e.g. Windows native Winsock without WSL). Addressed by targeting Linux/WSL2 and Docker.
