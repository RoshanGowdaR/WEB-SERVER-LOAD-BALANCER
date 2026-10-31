#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build"
RESULTS_DIR="$PROJECT_ROOT/benchmarks/results"

mkdir -p "$RESULTS_DIR"

echo "=========================================================="
echo " High-Performance C++ Load Balancer - Benchmark Harness"
echo "=========================================================="

# Build release if needed
if [ ! -f "$BUILD_DIR/load_balancer_server" ]; then
    echo "[!] Binary not found. Building project..."
    cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j4
fi

# Spin up 3 mock backends using Python in background
MOCK_PID_FILE="$RESULTS_DIR/backends.pid"
python3 - << 'EOF' > /dev/null 2>&1 &
import threading
from http.server import HTTPServer, BaseHTTPRequestHandler
import json

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        resp = b'{"status":"ok","handled_by":"mock_backend"}'
        self.send_header('Content-Length', str(len(resp)))
        self.end_headers()
        self.wfile.write(resp)
    def log_message(self, *args):
        pass

for port in [9001, 9002, 9003]:
    s = HTTPServer(('127.0.0.1', port), Handler)
    threading.Thread(target=s.serve_forever, daemon=True).start()

import time
while True:
    time.sleep(1)
EOF
BACKEND_PID=$!
echo $BACKEND_PID > "$MOCK_PID_FILE"
echo "[+] Mock backends launched (PID: $BACKEND_PID)"
sleep 1

# Launch Load Balancer
LB_CONFIG="$PROJECT_ROOT/config/config.example.json"
"$BUILD_DIR/load_balancer_server" -c "$LB_CONFIG" > "$RESULTS_DIR/server.log" 2>&1 &
LB_PID=$!
echo "[+] Load Balancer launched (PID: $LB_PID) on port 8080"
sleep 1.5

cleanup() {
    echo "[*] Cleaning up benchmark processes..."
    kill -SIGINT $LB_PID 2>/dev/null || true
    kill -9 $BACKEND_PID 2>/dev/null || true
    rm -f "$MOCK_PID_FILE"
    echo "[+] Cleaned up successfully."
}
trap cleanup EXIT

# 1. Warm-up
echo "[*] Warming up server..."
ab -n 1000 -c 10 http://127.0.0.1:8080/health > /dev/null 2>&1

# 2. Benchmark Concurrency: 10, 50, 100
for CONCURRENCY in 10 50 100; do
    TOTAL_REQS=5000
    OUTPUT_FILE="$RESULTS_DIR/ab_c${CONCURRENCY}.txt"
    echo "[*] Running ApacheBench: Requests: $TOTAL_REQS, Concurrency: $CONCURRENCY..."
    ab -n $TOTAL_REQS -c $CONCURRENCY http://127.0.0.1:8080/api/bench > "$OUTPUT_FILE" 2>&1
    
    # Extract Key Metrics
    RPS=$(grep "Requests per second:" "$OUTPUT_FILE" | awk '{print $4}')
    TIME_PER_REQ=$(grep "Time per request:" "$OUTPUT_FILE" | head -n 1 | awk '{print $4}')
    FAILED_REQS=$(grep "Failed requests:" "$OUTPUT_FILE" | awk '{print $3}')
    P95=$(grep "95%" "$OUTPUT_FILE" | awk '{print $2}')
    P99=$(grep "99%" "$OUTPUT_FILE" | awk '{print $2}')

    echo "    -> Throughput: $RPS req/sec"
    echo "    -> Mean Latency: $TIME_PER_REQ ms"
    echo "    -> P95 Latency: $P95 ms | P99 Latency: $P99 ms"
    echo "    -> Failed Requests: $FAILED_REQS"
done

echo "=========================================================="
echo " Benchmark suite completed successfully!"
echo " Results written to: $RESULTS_DIR"
echo "=========================================================="
