# Workloads

The deterministic workload generator (`WorkloadConfig`, `generate_workload`) produces a `Trace` from a seed.

## Dimensions

operation count, live allocation target, min/max size, size distribution, alignment distribution, alloc/free ratio, reallocation ratio, burstiness, lifetime distribution, temporal locality, object reuse, hot/cold classes, allocation domain, thread count, producer/consumer count, working-set target, seed, phase counts, steady-state/warm-up/cooldown, and release-to-zero.

## Size distributions

`fixed`, `uniform`, `log_uniform`, `geometric`, `bimodal`, `multimodal`, `power_law`, `histogram` (caller weights), and `caller_sequence`.

## Lifetime distributions

`immediate`, `short`, `medium`, `long`, `mixed`, `fixed`, `random`, `phase_bound`, `caller_provided`.

## Concurrency

Workloads can be generated with multiple worker threads. A concurrent run partitions the trace by worker; each worker stream executes in its own thread against a thread-safe allocator.

## Determinism

The same seed reproduces the identical trace. Replaying the same trace against multiple allocators is the basis of fair comparison.
