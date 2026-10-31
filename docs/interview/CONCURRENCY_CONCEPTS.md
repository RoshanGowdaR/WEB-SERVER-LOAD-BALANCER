# Concurrency Concepts Applied in This Project

1. **Producer-Consumer Pattern:**
   - The accept loop acts as the producer, placing incoming client connections into the task queue. The worker threads act as consumers, pulling tasks off the queue and processing HTTP lifecycles.

2. **Condition Variables & Zero Busy-Waiting:**
   - Workers wait on `cv_task_available_.wait(lock, [this]() { return stop_ || !tasks_.empty(); })`. When idle, worker threads are placed into kernel sleep states, consuming 0% CPU. When a connection arrives, `notify_one()` wakes exactly one worker.

3. **Lock Contention & Atomic Operations:**
   - Instead of locking a mutex on every metric increment, high-frequency counters (`total_requests_`, `active_connections_`) use lock-free `std::atomic<uint64_t>` operations (`fetch_add`, `store`, `load`), avoiding lock convoy effects.

4. **Exception Safety in Worker Threads:**
   - Tasks executed inside worker threads are wrapped in `try ... catch (const std::exception& ex)`. If an individual client request triggers an unhandled exception, it is logged and the worker remains alive to process subsequent tasks.
