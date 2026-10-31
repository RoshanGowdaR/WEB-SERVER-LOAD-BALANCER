# Contributing Guidelines

Thank you for your interest in contributing to the High-Performance C++ Load Balancer project!

## 1. Development Environment
- Linux / WSL2 (Ubuntu 22.04 or 24.04 recommended)
- GCC 13+ or Clang 16+ supporting C++20
- CMake 3.20+
- GoogleTest (`libgtest-dev`)

## 2. Code Standards
- Code formatting follows the Google C++ Style Guide configured in `.clang-format`.
- Run clang-format prior to creating pull requests:
  ```bash
  clang-format -i src/**/*.cpp include/**/*.h tests/**/*.cpp
  ```
- All classes managing operating system resources must adhere to strict RAII.

## 3. Pull Request Requirements
1. Every new feature must be accompanied by relevant unit and integration tests.
2. Ensure full test suite passes cleanly via `ctest --output-on-failure`.
3. Verify memory safety by building and running with AddressSanitizer enabled (`-DENABLE_ASAN=ON`).
