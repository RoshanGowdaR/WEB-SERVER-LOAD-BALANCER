#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build-release"
RESULTS_DIR="$PROJECT_ROOT/benchmarks/results"

mkdir -p "$RESULTS_DIR"

echo "=========================================================="
echo " High-Performance C++ Load Balancer - Benchmark Harness"
echo "=========================================================="

# Collect and record hardware / OS environment metadata
echo "[*] Collecting system specification..."
{
    echo "Date: $(date -u)"
    echo "OS: $(uname -srm)"
    echo "CPU: $(lscpu | grep 'Model name:' | sed 's/Model name:[ \t]*//' || echo 'Unknown')"
    echo "CPU Cores: $(nproc || echo 'Unknown')"
    echo "RAM: $(free -h | awk '/^Mem:/ {print $2}')"
    echo "Compiler: $(g++ --version | head -n 1)"
} > "$RESULTS_DIR/system_info.txt"
cat "$RESULTS_DIR/system_info.txt"

# Rebuild in Release mode to ensure latest compiled code is benchmarked
echo "[*] Building Release binary..."
cmake -B "$BUILD_DIR" -S "$PROJECT_ROOT" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build "$BUILD_DIR" -j4

# Check for port conflicts on 8080, 9001, 9002, 9003
for PORT in 8080 9001 9002 9003; do
    if lsof -Pi :$PORT -sTCP:LISTEN -t >/dev/null 2>&1; then
        echo "[!] Port $PORT is already in use. Terminating existing process..."
        fuser -k ${PORT}/tcp 2>/dev/null || true
    fi
done

# Spin up 3 multi-threaded mock backends using Python ThreadingHTTPServer
MOCK_PID_FILE="$RESULTS_DIR/backends.pid"
python3 - << 'EOF' > /dev/null 2>&1 &
import threading
from http.server import ThreadingHTTPServer, BaseHTTPRequestHandler
import json

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        port = self.server.server_address[1]
        node_id = f"backend-{port}"
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        resp = json.dumps({"status": "ok", "backend": node_id}).encode('utf-8')
        self.send_header('Content-Length', str(len(resp)))
        self.end_headers()
        self.wfile.write(resp)
    def log_message(self, *args):
        pass

for port in [9001, 9002, 9003]:
    s = ThreadingHTTPServer(('127.0.0.1', port), Handler)
    threading.Thread(target=s.serve_forever, daemon=True).start()

import time
while True:
    time.sleep(1)
EOF
BACKEND_PID=$!
echo $BACKEND_PID > "$MOCK_PID_FILE"
echo "[+] Multi-threaded mock backends launched (PID: $BACKEND_PID)"

# Verify backend readiness
for PORT in 9001 9002 9003; do
    READY=0
    for i in {1..20}; do
        if curl -s "http://127.0.0.1:$PORT/health" > /dev/null 2>&1; then
            READY=1
            break
        fi
        sleep 0.1
    done
    if [ $READY -ne 1 ]; then
        echo "[!] Failed to start backend on port $PORT"
        kill -9 $BACKEND_PID 2>/dev/null || true
        exit 1
    fi
done
echo "[+] All 3 backend servers verified active and healthy."

# Launch Load Balancer
LB_CONFIG="$PROJECT_ROOT/config/config.example.json"
"$BUILD_DIR/load_balancer_server" -c "$LB_CONFIG" > "$RESULTS_DIR/server.log" 2>&1 &
LB_PID=$!
echo "[+] Load Balancer launched (PID: $LB_PID) on port 8080"

cleanup() {
    echo "[*] Cleaning up benchmark processes..."
    kill -SIGINT $LB_PID 2>/dev/null || true
    kill -9 $BACKEND_PID 2>/dev/null || true
    rm -f "$MOCK_PID_FILE"
    echo "[+] Cleaned up successfully."
}
trap cleanup EXIT

# Verify load balancer readiness
LB_READY=0
for i in {1..30}; do
    if curl -s "http://127.0.0.1:8080/health" > /dev/null 2>&1; then
        LB_READY=1
        break
    fi
    sleep 0.1
done
if [ $LB_READY -ne 1 ]; then
    echo "[!] Load balancer failed to start on port 8080"
    exit 1
fi
echo "[+] Load balancer verified ready on http://127.0.0.1:8080"

# 1. Warm-up on the actual proxy forwarding path
echo "[*] Warming up reverse proxy forwarding path..."
ab -n 500 -c 10 http://127.0.0.1:8080/api/bench > /dev/null 2>&1 || true

# 2. Benchmark Concurrency levels: 10, 50, 100
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
