# Dependency Manifest

The project minimizes external runtime dependencies to keep networking and concurrency concepts transparent.

| Dependency | Version | Type | License | Purpose |
| :--- | :--- | :--- | :--- | :--- |
| **Linux POSIX API** | POSIX.1-2008 | System | - | Low-level socket networking (`sys/socket.h`, `poll.h`, `netinet/tcp.h`). |
| **C++ Standard Library**| ISO C++20 | Core Language | - | STL containers, `<thread>`, `<atomic>`, `<mutex>`, `<condition_variable>`. |
| **GoogleTest** | 1.14.0 | Development / Test | BSD-3-Clause | Unit testing, integration testing, and test assertions. |
| **ApacheBench (`ab`)** | 2.3 | Performance Tool | Apache-2.0 | Automated load generation and latency benchmarking. |
| **Python** | 3.10+ | Tooling / E2E | PSF | End-to-end system testing script orchestrator. |
