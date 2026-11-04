# Build Verification Report

**Date:** 2026-11-02  
**Platform:** Ubuntu 24.04 LTS on WSL2 (Kernel: Linux 6.6.114.1-microsoft-standard-WSL2 x86_64)  
**Host Hardware:** AMD Ryzen 7 7445HS (12 Cores), 7.4 GiB RAM  
**Compilers:** GCC 13.3.0, Clang 18.1.3  
**Build Tool:** CMake 3.28.3, GNU Make 4.3  

---

## 1. Clean Build Matrix Summary

All build configurations were executed independently from pristine build directories. Every build completed with zero warnings (under `-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wcast-align -Wunused -Woverloaded-virtual -Wformat=2`) and passed 100% of their test suites.

| Configuration | Build Directory | Compiler | Build Type | Flags / Sanitizers | Test Suites Passed | Overall Status |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Debug** | `build-debug/` | GCC 13.3.0 | Debug | `-g -O0` | 6 / 6 (100%) | **PASS** |
| **Release** | `build-release/` | GCC 13.3.0 | Release | `-O3 -DNDEBUG` | 6 / 6 (100%) | **PASS** |
| **ASan + UBSan** | `build-asan/` | GCC 13.3.0 | Debug | `-fsanitize=address,undefined` | 6 / 6 (100%) | **PASS** |
| **TSan** | `build-tsan/` | GCC 13.3.0 | Debug | `-fsanitize=thread` | 6 / 6 (100%) | **PASS** |
| **libFuzzer** | `build-fuzz/` | Clang 18.1.3 | RelWithDebInfo | `-fsanitize=fuzzer,address,undefined` | 2 Targets Built & Executed | **PASS** |

---

## 2. Configuration Details & Execution Logs

### 2.1 Debug Build (`build-debug/`)
- **Command:** `cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON && cmake --build build-debug -j4 && ctest --test-dir build-debug --output-on-failure`
- **CTest Results:**
  - `Test #1: UnitTests ........................   Passed    0.06 sec`
  - `Test #2: IntegrationTests .................   Passed    0.65 sec`
  - `Test #3: SecurityTests ....................   Passed    0.02 sec`
  - `Test #4: ConcurrencyTests .................   Passed    1.13 sec`
  - `Test #5: RegressionTests ..................   Passed    0.62 sec`
  - `Test #6: FuzzHttpParserSmoke ..............   Passed    0.22 sec`
- **Total Test Time:** 2.83 sec (100% passed)

### 2.2 Release Build (`build-release/`)
- **Command:** `cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON && cmake --build build-release -j4 && ctest --test-dir build-release --output-on-failure`
- **CTest Results:**
  - `Test #1: UnitTests ........................   Passed    0.06 sec`
  - `Test #2: IntegrationTests .................   Passed    0.64 sec`
  - `Test #3: SecurityTests ....................   Passed    0.02 sec`
  - `Test #4: ConcurrencyTests .................   Passed    1.22 sec`
  - `Test #5: RegressionTests ..................   Passed    0.62 sec`
  - `Test #6: FuzzHttpParserSmoke ..............   Passed    0.04 sec`
- **Total Test Time:** 2.72 sec (100% passed)

### 2.3 Address & UndefinedBehavior Sanitizer (`build-asan/`)
- **Command:** `cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DBUILD_TESTING=ON && cmake --build build-asan -j4 && ctest --test-dir build-asan --output-on-failure`
- **CTest Results:**
  - `Test #1: UnitTests ........................   Passed    0.28 sec`
  - `Test #2: IntegrationTests .................   Passed    0.76 sec`
  - `Test #3: SecurityTests ....................   Passed    0.05 sec`
  - `Test #4: ConcurrencyTests .................   Passed    1.32 sec`
  - `Test #5: RegressionTests ..................   Passed    0.72 sec`
  - `Test #6: FuzzHttpParserSmoke ..............   Passed    0.62 sec`
- **Total Test Time:** 3.88 sec (100% passed, zero memory leaks, zero bounds errors)

### 2.4 ThreadSanitizer (`build-tsan/`)
- **Command:** `cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug -DENABLE_TSAN=ON -DBUILD_TESTING=ON && cmake --build build-tsan -j4 && setarch x86_64 -R ctest --test-dir build-tsan --output-on-failure`
- **CTest Results:**
  - `Test #1: UnitTests ........................   Passed    0.17 sec`
  - `Test #2: IntegrationTests .................   Passed    0.69 sec`
  - `Test #3: SecurityTests ....................   Passed    0.06 sec`
  - `Test #4: ConcurrencyTests .................   Passed    1.26 sec`
  - `Test #5: RegressionTests ..................   Passed    0.65 sec`
  - `Test #6: FuzzHttpParserSmoke ..............   Passed    1.63 sec`
- **Total Test Time:** 4.60 sec (100% passed, zero data races, zero synchronization warnings)
