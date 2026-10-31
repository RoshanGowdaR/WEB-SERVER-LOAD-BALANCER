# Operating Systems Concepts Applied in This Project

1. **File Descriptors & Kernel Tables:**
   - In Linux, sockets are represented as integer file descriptors referencing open file table entries in the kernel. `::close(fd)` decrements the reference count and closes the underlying transport control block.

2. **I/O Multiplexing with `::poll()`:**
   - The accept loop does not block indefinitely on `accept()`. It invokes `::poll()` with a 500ms timeout. This allows the accept thread to periodically check shutdown flags (`running_.load()`) and exit gracefully without socket blocking.

3. **System Calls & User/Kernel Mode Transitions:**
   - Calls like `::recv()`, `::send()`, `::poll()`, and `::bind()` cross the user-to-kernel boundary. Minimizing system calls (e.g. caching buffer reads, using larger chunk sizes) improves execution efficiency.

4. **Signals & Process Lifecycle:**
   - POSIX signals `SIGINT` (Ctrl+C) and `SIGTERM` are registered with custom handlers to trigger deterministic thread joining and socket cleanup rather than hard aborts.
