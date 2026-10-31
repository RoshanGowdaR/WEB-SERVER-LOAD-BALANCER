# Benchmarking Suite

This directory contains automated scripts and configuration profiles for evaluating the throughput and latency characteristics of the load balancer.

## Running the Automated Benchmark
Ensure `apache2-utils` is installed:
```bash
sudo apt install -y apache2-utils
```
Execute the benchmark harness:
```bash
./benchmarks/run_benchmark.sh
```
Results and raw output logs will be saved to `benchmarks/results/`.
