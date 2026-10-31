# Data Structures & Algorithms Applied in This Project

1. **Queue (`std::queue` / FIFO):**
   - Used in `ThreadPool` for first-in, first-out task scheduling.
   - Time Complexity: $O(1)$ push and $O(1)$ pop operations.

2. **Circular Buffer:**
   - Used in `monitoring::Metrics` to track recent latency samples (`LATENCY_WINDOW = 2048`).
   - Time Complexity: $O(1)$ amortized insert, bounded memory footprint.

3. **Round Robin Algorithm:**
   - Increments an atomic counter modulo the number of backends: $\text{idx} = (\text{counter}++) \pmod N$.
   - Time Complexity: $O(1)$ best case, $O(N)$ worst case when filtering unhealthy nodes.

4. **Least Connections Selection:**
   - Linear scan over healthy backends to find the instance with $\min(\text{active\_connections})$.
   - Time Complexity: $O(N)$ where $N$ is the number of backends. For typical cluster sizes ($N \le 100$), this runs in microseconds in L1 CPU cache.

5. **Sorting Algorithm (`std::sort` / Introsort):**
   - Used when computing percentile latencies (P50, P95, P99) over the latency window buffer.
   - Time Complexity: $O(K \log K)$ where $K = 2048$.

6. **Case-Insensitive String Comparison (Lexicographical Compare):**
   - Used as custom comparator for HTTP headers map (`std::map` Red-Black Tree).
   - Time Complexity: $O(L \log H)$ where $L$ is header length and $H$ is number of unique headers.
