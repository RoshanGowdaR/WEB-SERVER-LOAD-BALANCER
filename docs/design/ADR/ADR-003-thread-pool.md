# ADR-003: Fixed Worker ThreadPool with Bounded Queue Backpressure

## Status
Accepted

## Context
Handling concurrent network connections requires a concurrency model. Thread-per-request architectures create unbounded OS threads under high connection volume, resulting in kernel memory exhaustion and excessive context switching latency.

## Decision
Implement a reusable `ThreadPool` with a fixed number of worker threads (defaulting to hardware concurrency) and a strictly bounded task queue with an explicit backpressure rejection policy.

## Consequences
- **Positive:** Bound on maximum concurrent OS threads and RAM consumption; predictable performance under load; graceful rejection (503 Service Unavailable) instead of process crashes.
- **Negative:** Highly saturated queues reject connections if backend latency spikes. Solved by tuning worker threads and queue limits via configuration.
