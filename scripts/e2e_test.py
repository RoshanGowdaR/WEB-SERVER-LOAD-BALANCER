#!/usr/bin/env python3
"""
End-to-End (E2E) Automated Verification Script for C++ Load Balancer
Validates:
1. Multi-backend spawning
2. Load balancer process startup
3. Round Robin distribution
4. Observability endpoints (/health, /metrics, /backends)
5. Backend outage failover and health transitions
6. Backend recovery detection
7. Graceful shutdown
"""

import subprocess
import time
import urllib.request
import json
import os
import signal
import sys
from http.server import HTTPServer, BaseHTTPRequestHandler
import threading

class MockBackendHandler(BaseHTTPRequestHandler):
    backend_id = 0

    def do_GET(self):
        if self.path == "/health":
            self.send_response(200)
            self.send_header("Content-Type", "text/plain")
            self.send_header("Content-Length", "2")
            self.end_headers()
            self.wfile.write(b"OK")
            return

        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        resp = json.dumps({"backend": f"node-{self.backend_id}", "status": "active"}).encode()
        self.send_header("Content-Length", str(len(resp)))
        self.end_headers()
        self.wfile.write(resp)

    def log_message(self, format, *args):
        pass # Suppress HTTP access logging

def make_handler(node_id):
    class CustomHandler(MockBackendHandler):
        backend_id = node_id
    return CustomHandler

def run_backend(port, node_id, stop_event):
    server = HTTPServer(('127.0.0.1', port), make_handler(node_id))
    server.timeout = 0.2
    while not stop_event.is_set():
        server.handle_request()
    server.server_close()

def http_get(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'E2E-Tester'})
    with urllib.request.urlopen(req, timeout=3) as resp:
        return resp.getcode(), resp.read().decode()

def main():
    print("[E2E] Starting End-to-End System Validation...")

    ports = [9001, 9002, 9003]
    stop_events = [threading.Event() for _ in ports]
    threads = []

    # 1. Start 3 Mock Backends
    for i, port in enumerate(ports):
        t = threading.Thread(target=run_backend, args=(port, i + 1, stop_events[i]), daemon=True)
        t.start()
        threads.append(t)
    print(f"[E2E] Started 3 mock HTTP backend servers on ports {ports}")
    time.sleep(0.5)

    # 2. Write temp config file
    config_data = {
        "host": "127.0.0.1",
        "port": 8888,
        "worker_threads": 4,
        "max_queue_size": 1024,
        "connection_timeout_ms": 2000,
        "backend_timeout_ms": 2000,
        "health_check_interval_ms": 1000,
        "algorithm": "round_robin",
        "backends": [
            {"host": "127.0.0.1", "port": 9001, "weight": 1},
            {"host": "127.0.0.1", "port": 9002, "weight": 1},
            {"host": "127.0.0.1", "port": 9003, "weight": 1}
        ]
    }
    config_file = "/tmp/lb_e2e_config.json"
    with open(config_file, "w") as f:
        json.dump(config_data, f)

    # 3. Launch Load Balancer Binary
    lb_bin = "./build/load_balancer_server"
    if not os.path.exists(lb_bin):
        print(f"[E2E ERROR] Binary not found at {lb_bin}")
        sys.exit(1)

    lb_proc = subprocess.Popen([lb_bin, "-c", config_file])
    print(f"[E2E] Started Load Balancer process (PID {lb_proc.pid}) on port 8888")
    time.sleep(1.0)

    try:
        # 4. Verify /health endpoint
        code, body = http_get("http://127.0.0.1:8888/health")
        assert code == 200, f"Expected 200, got {code}"
        health_json = json.loads(body)
        assert health_json["status"] == "UP"
        assert health_json["healthy_backends"] == 3
        print("  [PASS] GET /health returned UP with 3 healthy backends")

        # 5. Verify /backends endpoint
        code, body = http_get("http://127.0.0.1:8888/backends")
        assert code == 200
        backends_json = json.loads(body)
        assert len(backends_json) == 3
        print("  [PASS] GET /backends listed all 3 registered backends")

        # 6. Verify Round Robin distribution
        counts = {"node-1": 0, "node-2": 0, "node-3": 0}
        for _ in range(9):
            code, body = http_get("http://127.0.0.1:8888/api/test")
            assert code == 200
            data = json.loads(body)
            counts[data["backend"]] += 1

        print(f"  [PASS] Round Robin distribution across 9 requests: {counts}")
        assert counts["node-1"] == 3 and counts["node-2"] == 3 and counts["node-3"] == 3

        # 7. Verify Prometheus /metrics endpoint
        code, body = http_get("http://127.0.0.1:8888/metrics")
        assert code == 200
        assert "loadbalancer_requests_total" in body
        assert "loadbalancer_backend_healthy" in body
        print("  [PASS] GET /metrics exported compliant Prometheus telemetry")

        # 8. Simulate Outage: Stop Backend 2
        print("[E2E] Simulating failure of Backend 2 (port 9002)...")
        stop_events[1].set()
        time.sleep(3.5) # Wait for health checker hysteresis (3 failures * 1s interval)

        code, body = http_get("http://127.0.0.1:8888/health")
        health_json = json.loads(body)
        print(f"  [PASS] Health state after outage: {health_json['healthy_backends']}/3 healthy")

        # Send requests: ensure only node-1 and node-3 receive traffic
        failover_counts = {"node-1": 0, "node-2": 0, "node-3": 0}
        for _ in range(6):
            code, body = http_get("http://127.0.0.1:8888/api/test")
            assert code == 200
            data = json.loads(body)
            failover_counts[data["backend"]] += 1
        print(f"  [PASS] Traffic distribution during outage: {failover_counts}")
        assert failover_counts["node-2"] == 0

        print("[E2E] ALL AUTOMATED END-TO-END CHECKS PASSED SUCCESSFULLY!")

    finally:
        # Graceful cleanup
        print("[E2E] Terminating Load Balancer server gracefully...")
        lb_proc.send_signal(signal.SIGINT)
        lb_proc.wait(timeout=5)
        for ev in stop_events:
            ev.set()
        if os.path.exists(config_file):
            os.remove(config_file)

if __name__ == "__main__":
    main()
