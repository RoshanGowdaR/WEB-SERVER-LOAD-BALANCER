# Coding Standards & Guidelines

1. **Modern C++20 Idioms**:
   - Prefer `std::string_view` for read-only string parameters.
   - Use `[[nodiscard]]` on non-mutating query methods and factory functions.
   - Use `noexcept` on destructors, move constructors, move assignment operators, and non-throwing queries.

2. **Resource Management**:
   - Zero raw pointer ownership (`new` / `delete`). Use `std::unique_ptr` and `std::shared_ptr`.
   - Wrap all operating system handles (sockets, file descriptors) in move-only RAII classes.

3. **Concurrency & Thread Safety**:
   - Protect mutable shared state with `std::mutex`, `std::shared_mutex`, or atomic variables.
   - Always acquire locks using `std::lock_guard` or `std::unique_lock`.
   - Never perform blocking I/O while holding a mutex that guards task queues or shared state.

4. **Formatting**:
   - Adhere to the Google C++ style defined in `.clang-format` (4-space indentation, 100-character line limit).
