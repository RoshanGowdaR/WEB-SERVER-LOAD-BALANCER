# Benchmark Configuration Profiles

## Profile 1: Low Concurrency (Latency Sensitive)
- **Concurrency:** 10
- **Total Requests:** 5,000
- **Target:** Measure minimum processing overhead and tail latency baseline.

## Profile 2: Medium Concurrency (Target Workload)
- **Concurrency:** 50
- **Total Requests:** 5,000
- **Target:** Measure throughput scalability with active worker pool saturation.

## Profile 3: High Concurrency (Stress & Backpressure)
- **Concurrency:** 100
- **Total Requests:** 5,000
- **Target:** Verify zero connection drops and bounded latency under heavy concurrent contention.
