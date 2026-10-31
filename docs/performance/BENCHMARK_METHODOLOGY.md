# Benchmark Methodology

## 1. Test Harness Design
The benchmark harness (`benchmarks/run_benchmark.sh`) automates reproducible performance evaluations:
1. Compiles the optimized Release binary (`cmake -DCMAKE_BUILD_TYPE=Release`).
2. Spawns 3 dedicated mock HTTP backend services in the background.
3. Launches the load balancer configured with 8 worker threads and Round Robin routing.
4. Executes a warm-up phase (1,000 requests) to prime socket buffers and thread pool structures.
5. Executes tiered load tests with ApacheBench across concurrency levels 10, 50, and 100.
6. Measures throughput, mean latency, percentile latencies (P95, P99), and failed connections.
7. Shuts down all processes cleanly via signals.

## 2. Measurement Tools
- **ApacheBench (`ab`)**: Measures client-perceived connection latency and throughput.
- **Internal Metrics Engine (`/metrics`)**: Measures internal service duration and active connection counts.
