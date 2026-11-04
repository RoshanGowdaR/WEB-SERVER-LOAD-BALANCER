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

cleanup() {
    echo "[*] Cleaning up benchmark processes..."
    kill -SIGINT $LB_PID 2>/dev/null || true
    kill -9 $BACKEND_PID 2>/dev/null || true
    rm -f "$MOCK_PID_FILE" "$RESULTS_DIR/config_temp.json"
    echo "[+] Cleaned up successfully."
}
trap cleanup EXIT

for ALGO in round_robin least_connections; do
    echo ""
    echo "=========================================================="
    echo " Benchmarking Algorithm: $ALGO"
    echo "=========================================================="

    # Create algorithm-specific configuration
    sed 's/"algorithm": "[^"]*"/"algorithm": "'"$ALGO"'"/' "$PROJECT_ROOT/config/config.example.json" > "$RESULTS_DIR/config_temp.json"

    # Launch Load Balancer
    "$BUILD_DIR/load_balancer_server" -c "$RESULTS_DIR/config_temp.json" > "$RESULTS_DIR/server_${ALGO}.log" 2>&1 &
    LB_PID=$!
    echo "[+] Load Balancer ($ALGO) launched (PID: $LB_PID) on port 8080"

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
        echo "[!] Load balancer failed to start for $ALGO"
        exit 1
    fi
    echo "[+] Load balancer verified ready on http://127.0.0.1:8080"

    # Warm-up on proxy forwarding path
    echo "[*] Warming up reverse proxy forwarding path ($ALGO)..."
    ab -n 500 -c 10 http://127.0.0.1:8080/api/bench > /dev/null 2>&1 || true

    # Benchmark Concurrency levels: 10, 50, 100 across 3 repetitions
    for CONCURRENCY in 10 50 100; do
        TOTAL_REQS=5000
        echo "[*] Benchmarking Algorithm: $ALGO | Concurrency: $CONCURRENCY (3 repetitions of $TOTAL_REQS requests)..."

        for REP in 1 2 3; do
            REP_FILE="$RESULTS_DIR/ab_${ALGO}_c${CONCURRENCY}_rep${REP}.txt"
            echo "    -> Run $REP/3..."
            ab -n $TOTAL_REQS -c $CONCURRENCY http://127.0.0.1:8080/api/bench > "$REP_FILE" 2>&1
        done

        # Retain canonical file for backwards compatibility
        OUTPUT_FILE="$RESULTS_DIR/ab_${ALGO}_c${CONCURRENCY}.txt"
        cp "$RESULTS_DIR/ab_${ALGO}_c${CONCURRENCY}_rep1.txt" "$OUTPUT_FILE"
        if [ "$ALGO" = "round_robin" ]; then
            cp "$OUTPUT_FILE" "$RESULTS_DIR/ab_c${CONCURRENCY}.txt"
        fi

        # Compute aggregate statistics over 3 runs
        python3 - << PYEOF
import re, math

rps_vals, lat_vals, p95_vals, p99_vals = [], [], [], []
failed_total = 0

for rep in [1, 2, 3]:
    fpath = f"$RESULTS_DIR/ab_${ALGO}_c${CONCURRENCY}_rep{rep}.txt"
    try:
        with open(fpath, "r") as f:
            content = f.read()
        m_rps = re.search(r"Requests per second:\s+([0-9.]+)", content)
        m_lat = re.search(r"Time per request:\s+([0-9.]+)\s+\[ms\]\s+\(mean\)", content)
        m_fail = re.search(r"Failed requests:\s+([0-9]+)", content)
        m_p95 = re.search(r"95%\s+([0-9]+)", content)
        m_p99 = re.search(r"99%\s+([0-9]+)", content)
        if m_rps: rps_vals.append(float(m_rps.group(1)))
        if m_lat: lat_vals.append(float(m_lat.group(1)))
        if m_p95: p95_vals.append(float(m_p95.group(1)))
        if m_p99: p99_vals.append(float(m_p99.group(1)))
        if m_fail: failed_total += int(m_fail.group(1))
    except Exception:
        pass

def stats(vals):
    if not vals: return 0.0, 0.0, 0.0, 0.0
    mean = sum(vals) / len(vals)
    var = sum((x - mean)**2 for x in vals) / len(vals)
    return mean, math.sqrt(var), min(vals), max(vals)

m_rps, sd_rps, min_rps, max_rps = stats(rps_vals)
m_lat, sd_lat, min_lat, max_lat = stats(lat_vals)
m_p95 = sum(p95_vals) / len(p95_vals) if p95_vals else 0.0
m_p99 = sum(p99_vals) / len(p99_vals) if p99_vals else 0.0

print(f"    [AGGREGATE 3-RUN SUMMARY]")
print(f"      Throughput (req/s): Mean={m_rps:.2f} (±{sd_rps:.2f}) [Min={min_rps:.2f}, Max={max_rps:.2f}]")
print(f"      Latency (ms)      : Mean={m_lat:.2f} ms (±{sd_lat:.2f}) [Min={min_lat:.2f}, Max={max_lat:.2f}]")
print(f"      Tail Latency (ms) : P95={m_p95:.1f} ms | P99={m_p99:.1f} ms")
print(f"      Failed Requests   : {failed_total} total across 3 runs")
PYEOF
    done

    # Gracefully stop the load balancer before switching algorithms
    kill -SIGINT $LB_PID 2>/dev/null || true
    wait $LB_PID 2>/dev/null || true
    sleep 0.5
done

echo "=========================================================="
echo " Benchmark suite completed successfully!"
echo " Results written to: $RESULTS_DIR"
echo "=========================================================="
