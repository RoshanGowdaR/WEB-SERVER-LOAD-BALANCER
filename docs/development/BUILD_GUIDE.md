# Build Guide

## 1. Prerequisites
- **Operating System:** Linux (Ubuntu 22.04 / 24.04) or Windows WSL2
- **Compiler:** GCC 13.3+ or Clang 16+ supporting C++20
- **Build System:** CMake 3.20+
- **Libraries:** `libgtest-dev` (GoogleTest), `apache2-utils` (for benchmarking)

Install dependencies on Ubuntu / WSL2:
```bash
sudo apt update
sudo apt install -y build-essential cmake git libgtest-dev apache2-utils python3
```

---

## 2. Compilation Workflows

### Standard Release Build (Optimized)
```bash
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build -j$(nproc)
```

### Debug Build with Symbols
```bash
cmake -B build-debug -S . -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-debug -j$(nproc)
```

### AddressSanitizer (ASan) & UndefinedBehaviorSanitizer (UBSan) Build
```bash
cmake -B build-asan -S . -DENABLE_ASAN=ON -DBUILD_TESTING=ON
cmake --build build-asan -j$(nproc)
```

### ThreadSanitizer (TSan) Build
```bash
cmake -B build-tsan -S . -DENABLE_TSAN=ON -DBUILD_TESTING=ON
cmake --build build-tsan -j$(nproc)
```

---

## 3. Running the Test Suite
```bash
cd build && ctest --output-on-failure
```
Or run the individual test executables:
```bash
./build/tests/unit_tests
./build/tests/integration_tests
./build/tests/regression_tests
./build/tests/fuzz_http_parser
python3 scripts/e2e_test.py
```
