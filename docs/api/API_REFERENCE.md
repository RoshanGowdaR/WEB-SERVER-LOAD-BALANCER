# API Reference

The reverse proxy exposes built-in observability and management endpoints that are intercepted locally without contacting upstream backends.

---

## 1. Administrative Endpoints

### `GET /health`
Returns overall health status of the load balancer and healthy backend count.

- **Status Code:** `200 OK`
- **Content-Type:** `application/json; charset=utf-8`
- **Response Example:**
```json
{
  "status": "UP",
  "healthy_backends": 3,
  "total_backends": 3,
  "active_connections": 1,
  "total_requests": 1420
}
```

---

### `GET /metrics`
Exports operational telemetry in Prometheus-compatible exposition format.

- **Status Code:** `200 OK`
- **Content-Type:** `text/plain; version=0.0.4; charset=utf-8`
- **Response Example:**
```text
# HELP loadbalancer_requests_total Total number of HTTP requests received
# TYPE loadbalancer_requests_total counter
loadbalancer_requests_total 15000

# HELP loadbalancer_requests_successful_total Total successful requests forwarded
# TYPE loadbalancer_requests_successful_total counter
loadbalancer_requests_successful_total 14998

# HELP loadbalancer_active_connections Current active connections
# TYPE loadbalancer_active_connections gauge
loadbalancer_active_connections 4

# HELP loadbalancer_latency_avg_ms Average request latency in milliseconds
# TYPE loadbalancer_latency_avg_ms gauge
loadbalancer_latency_avg_ms 4.694

# HELP loadbalancer_latency_p95_ms 95th percentile latency in milliseconds
# TYPE loadbalancer_latency_p95_ms gauge
loadbalancer_latency_p95_ms 8.000

# HELP loadbalancer_backend_healthy Backend health status (1 = healthy, 0 = unhealthy)
# TYPE loadbalancer_backend_healthy gauge
loadbalancer_backend_healthy{backend="127.0.0.1:9001"} 1
loadbalancer_backend_healthy{backend="127.0.0.1:9002"} 1
loadbalancer_backend_healthy{backend="127.0.0.1:9003"} 1
```

---

### `GET /backends`
Lists all configured backend servers, health statuses, and per-backend metrics.

- **Status Code:** `200 OK`
- **Content-Type:** `application/json; charset=utf-8`
- **Response Example:**
```json
[
  {
    "address": "127.0.0.1:9001",
    "host": "127.0.0.1",
    "port": 9001,
    "healthy": true,
    "active_connections": 2,
    "total_requests": 5000,
    "total_failures": 0
  },
  {
    "address": "127.0.0.1:9002",
    "host": "127.0.0.1",
    "port": 9002,
    "healthy": true,
    "active_connections": 1,
    "total_requests": 5000,
    "total_failures": 0
  }
]
```
