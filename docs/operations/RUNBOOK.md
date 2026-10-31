# Operations Runbook

## 1. Daily Operations & Health Verification
- Check service status:
  ```bash
  curl -s http://127.0.0.1:8080/health | jq .
  ```
- Check registered upstream status:
  ```bash
  curl -s http://127.0.0.1:8080/backends | jq .
  ```

## 2. Alert Conditions & Remediation
- **Condition: `status: "DEGRADED"`**:
  - Cause: One or more backends have failed consecutive health probes.
  - Action: Inspect backend logs on corresponding host/port to identify crash or port exhaustion.
- **Condition: High Rejection Rate (`loadbalancer_requests_rejected_total` increasing)**:
  - Cause: Traffic arrival rate exceeds worker capacity and task queue is full.
  - Action: Increase `worker_threads` and `max_queue_size` in config file; verify backend response latency.
- **Condition: High Tail Latency (P99 > 100ms)**:
  - Cause: Upstream backend slowdown or network saturation.
  - Action: Inspect per-backend latency metrics in Prometheus.
