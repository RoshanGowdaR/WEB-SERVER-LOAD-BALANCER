# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-10-31

### Added
- **Core Networking:** RAII `net::Socket`, `net::ServerSocket`, and `net::ClientSocket` with POSIX APIs (`SO_REUSEADDR`, `SO_REUSEPORT`, `TCP_NODELAY`, non-blocking connect timeouts).
- **HTTP/1.1 Engine:** Streaming incremental `HttpParser` supporting `GET`, `POST`, `PUT`, `DELETE`, `HEAD`, and `OPTIONS`.
- **Concurrency:** Bounded `ThreadPool` with worker exception safety and queue backpressure overload protection.
- **Load Balancing:** Dynamic `RoundRobinBalancer` and `LeastConnectionsBalancer` with thread-safe backend selection.
- **Reliability:** Background `HealthChecker` service with hysteresis failure/recovery tracking.
- **Reverse Proxy:** Upstream request forwarding, header injection (`X-Forwarded-For`, `Via`), and synthetic error generation (502, 503, 504).
- **Observability:** Built-in endpoints `/health`, `/metrics` (Prometheus-compatible), and `/backends` (JSON).
- **Testing:** 70 verified test runs across unit, integration, regression, fuzzing, and end-to-end suites.
- **DevOps:** Multi-stage `Dockerfile`, `docker-compose.yml`, and GitHub Actions CI/CD workflows.
