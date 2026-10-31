# Shutdown and Recovery Guide

## 1. Graceful Shutdown Protocol
When the server receives `SIGINT` (Ctrl+C) or `SIGTERM`, it executes a graceful multi-stage shutdown:

```
[Signal Received (SIGINT/SIGTERM)]
             │
             ▼
[1. Stop Accept Loop] ──────── Closes ServerSocket to stop accepting new TCP connections
             │
             ▼
[2. Stop Health Checker] ───── Terminates background probing thread cleanly
             │
             ▼
[3. Drain ThreadPool] ──────── Finishes all currently enqueued requests; joins workers
             │
             ▼
[4. Clean Exit (Code 0)] ───── Process exits cleanly with zero resource leaks
```

## 2. Disaster Recovery Protocol
If an entire upstream server farm experiences a power or network partition:
1. The load balancer marks all backends as unhealthy after $N$ consecutive health check failures.
2. The load balancer returns `503 Service Unavailable` with clean error JSON to prevent cascade failures.
3. Once backends are restored, `HealthChecker` detects $M$ consecutive successful probes and automatically re-admits recovered instances back into the active rotation with zero operator intervention.
