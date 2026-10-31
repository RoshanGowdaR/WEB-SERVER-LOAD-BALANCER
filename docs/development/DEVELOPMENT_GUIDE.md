# Development Guide

## 1. Local Development Setup
Clone the repository and initialize the project:
```bash
git clone https://github.com/RoshanGowdaR/WEB-SERVER-LOAD-BALANCER.git
cd WEB-SERVER-LOAD-BALANCER
cmake -B build -S .
cmake --build build -j4
```

## 2. Running the Server Locally
To start the load balancer with the example configuration:
```bash
./build/load_balancer_server -c config/config.example.json -v
```

Options:
- `-c, --config <file>`: Configuration JSON file path.
- `-p, --port <port>`: Override listen port.
- `-a, --algo <algorithm>`: Override algorithm (`round_robin` or `least_connections`).
- `-v, --verbose`: Enable DEBUG logging level.

## 3. Code Organization
- `include/`: Public headers organized by subsystem (`network`, `http`, `threading`, `balancer`, `health`, `proxy`, `monitoring`, `config`, `logging`, `server`).
- `src/`: Implementation files corresponding directly to the public headers.
- `tests/`: Categorized test suites (`unit`, `integration`, `regression`, `fuzz`).
- `config/`: JSON configuration templates.
- `benchmarks/`: Benchmark harness and results.
- `scripts/`: System verification and automated test runners.
