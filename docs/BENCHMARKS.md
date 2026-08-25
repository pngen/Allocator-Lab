# Benchmarks

The benchmark suite (`allocator-lab-benchmarks`) compares the built-in strategies across several workloads and reports throughput and allocation latency.

## Workloads

tiny fixed-size, uniform mixed, log-uniform mixed, bimodal, bursty, long-lived + short-lived churn, checkerboard fragmentation, realloc-heavy, concurrent, capacity pressure, and trace replay.

## Measurement discipline

Workloads are seeded; setup cost is separated from steady-state; raw latency samples are captured up to a bounded limit; throughput is sanity-checked against the operation count and clock.

## Interpreting the numbers

- A pooling allocator's throughput often improves as its steady-state reuse rises, but it may retain backing after the workload.
- An arena allocator is fast because individual free is absent; label that semantic difference.
- A buddy allocator pays internal fragmentation from power-of-two rounding.
- A free-list allocator pays search and coalescing costs; external fragmentation is visible.
- Pinned and CUDA allocations have intentionally high host-side setup cost.
- CUDA async/pool allocation can improve repeated workloads, but only synchronized completed work is a meaningful latency.
- Contention can change the ranking; a strategy can win throughput and lose p99, or win memory efficiency and lose latency. No single composite score is universal unless weights are explicitly configured.
