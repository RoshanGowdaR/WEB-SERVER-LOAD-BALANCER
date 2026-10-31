# Threading Model & Concurrency Architecture

## 1. Thread Hierarchy & Roles

The system employs a multi-threaded architecture with explicit role separation:

```
[Main Thread] ── Spawns & supervises all subsystems; handles SIGINT/SIGTERM.
      │
      ├── [Acceptor Thread] ────── Runs TcpServer::accept_loop() using ::poll()
      │
      ├── [Worker Thread 1] ┐
      ├── [Worker Thread 2] │
      ├── [Worker Thread 3] ├───── Managed by ThreadPool (Fixed worker pool)
      ├── [Worker Thread ...]│     Processes client connections & proxies upstream
      └── [Worker Thread N] ┘
      │
      └── [HealthChecker Thread] ─ Periodic background thread probing upstream health
```

---

## 2. Synchronization Mechanisms & Safety

| Resource / Shared State | Access Pattern | Synchronization Mechanism | Rationale |
| :--- | :--- | :--- | :--- |
| **ThreadPool Task Queue** | Multiple producers, multiple consumers | `std::mutex` + 2 `std::condition_variable` | Zero busy-waiting; signals task availability and queue space |
| **Backend Health State** | Read by workers; updated by HealthChecker | `std::atomic<bool>`, `std::atomic<uint32_t>` | Lock-free reads in hot request routing paths |
| **Backend Active Conns** | Incremented/decremented by worker threads | `std::atomic<uint32_t>` with `BackendConnectionGuard` | Lock-free RAII tracking; prevents connection leakage |
| **Load Balancer Backend List** | Reads dominate; rare backend reconfiguration | `std::shared_mutex` (Reader-Writer lock) | Multiple concurrent reads (`std::shared_lock`); exclusive writes |
| **Metrics Counters** | High-frequency concurrent increments | `std::atomic<uint64_t>`, `std::atomic<uint32_t>` | Lock-free atomic operations (`fetch_add`, `compare_exchange_weak`) |
| **Latency Sample Buffer** | Samples collected for percentiles | `std::mutex` | Guarding circular buffer insertion and sorting |
| **Logging Output** | Concurrent log messages from all threads | `std::mutex` inside `Logger` | Prevents interleaved stdout / stderr lines |

---

## 3. Backpressure & Overload Protection

Unbounded task queues under traffic spikes lead to memory exhaustion (OOM crashes) and severe latency degradation.
To prevent this:
- `ThreadPool` has a fixed upper bound (`max_queue_size`, e.g., 2048 tasks).
- When the queue is full:
  - In `Reject` mode, the task is rejected immediately.
  - `TcpServer` intercepts the rejection and replies to the client with `HTTP/1.1 503 Service Unavailable` with `Retry-After: 5`, closing the connection cleanly.
  - Rejection metrics are incremented for monitoring and alerting.
