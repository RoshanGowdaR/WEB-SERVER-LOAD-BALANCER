# Troubleshooting Guide

| Symptom | Root Cause | Solution |
| :--- | :--- | :--- |
| **`Failed to bind and listen on 127.0.0.1:8080`** | Port 8080 is already bound by another process | Check running processes with `sudo lsof -i :8080` or `netstat -tuln \| grep 8080`. Terminate conflicting process or change port via `-p <port>`. |
| **`HTTP 502 Bad Gateway` returned to clients** | Load balancer cannot connect to the chosen upstream backend | Verify that upstream backend servers are running and listening on the designated IP and port. Verify firewall/iptables rules. |
| **`HTTP 503 Service Unavailable` returned** | Either all configured backends are marked unhealthy, or task queue is saturated | Check `/health` endpoint to inspect backend statuses. Check server log for `ThreadPool overloaded` warnings. |
| **`HTTP 504 Gateway Timeout` returned** | Upstream server accepted connection but did not send full response before `backend_timeout_ms` | Increase `backend_timeout_ms` in `config.json` or optimize backend endpoint performance. |
| **Process exits immediately on launch** | Configuration validation failed | Check console output for specific validation error (e.g., port 0, missing backends, or invalid algorithm). |
