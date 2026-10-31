# Debugging Guide

## 1. Using GDB for Crash & Core Dump Analysis
Build in Debug mode with full debug symbols:
```bash
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug -j4
gdb --args ./build-debug/load_balancer_server -c config/config.example.json -v
```

Common GDB commands:
- `r`: Run
- `bt`: Backtrace (inspect stack trace on SIGSEGV / crash)
- `info threads`: List active threads
- `thread apply all bt`: Print stack traces for all active threads

## 2. Diagnosing Memory Leaks and Overflows with ASan
Build with AddressSanitizer:
```bash
cmake -B build-asan -S . -DENABLE_ASAN=ON
cmake --build build-asan -j4
./build-asan/tests/unit_tests
```

## 3. Diagnosing Data Races with TSan
Build with ThreadSanitizer:
```bash
cmake -B build-tsan -S . -DENABLE_TSAN=ON
cmake --build build-tsan -j4
./build-tsan/tests/integration_tests
```

## 4. Networking Inspection with `tcpdump` & `curl`
Inspect loopback HTTP traffic:
```bash
sudo tcpdump -i lo -nn -s0 -A 'tcp port 8080'
```
Test endpoints with detailed timing:
```bash
curl -v -w "\nTime: %{time_total}s\n" http://127.0.0.1:8080/health
```
