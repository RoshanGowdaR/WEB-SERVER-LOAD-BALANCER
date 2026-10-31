# ADR-001: Adoption of C++20 Standard

## Status
Accepted

## Context
When building a modern systems engineering application, choosing the C++ language standard impacts compiler compatibility, concurrency primitives, standard library capabilities, and code expressiveness.

## Decision
Adopt ISO C++20 (`-std=c++20`) across all libraries, executables, and test harnesses.

## Consequences
- **Positive:** Enables `std::string_view`, advanced `<chrono>` operations, structured bindings, modern atomic memory models, and zero-overhead abstractions.
- **Negative:** Requires a modern compiler (GCC 11+ or Clang 13+). Fully satisfied by GCC 13.3 in Ubuntu 24.04.
