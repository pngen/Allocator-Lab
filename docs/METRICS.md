# Metrics

Allocator Lab measures real allocator behavior. Metrics are reported from the allocator's own accounting; a metric a backend cannot observe is marked unsupported rather than fabricated.

## Latency

Allocation, free, and reallocation latency are measured with a monotonic high-resolution clock and reported as mean, median (p50), p90, p95, p99, p99.9 (where sample count allows), max, and min. Raw samples are retained up to a bounded limit for exact percentiles; beyond that a bounded log2 histogram provides estimates.

## Throughput

allocations/sec, frees/sec, operations/sec, bytes allocated/sec, bytes freed/sec.

## Live and reserved state

live allocation count, live bytes, peak live allocations, peak live bytes, reserved bytes, committed bytes, peak reserved, peak committed.

## Accounting

total requested bytes, total granted bytes, internal waste, alignment waste, allocator metadata overhead (where measurable), reused bytes, fresh bytes, released bytes, stranded bytes.

## Fragmentation

internal fragmentation, external fragmentation (1 - largest_free_span / free_bytes), largest free span, free-span count, mean free-span size, free bytes, reserved-vs-live ratio, committed-vs-live ratio, size-class waste, alignment waste. When a backend exposes no free-span topology, the external metric is labeled unsupported.

## Reuse

pool hit rate, fresh allocation rate, reuse count, reuse bytes, same-size reuse, cross-size-class reuse, average reuse distance, reuse lifetime, trim recovery, backing retained after workload completion.

## Tail latency

A strategy with a good mean and a disastrous p99 is not equivalent to a consistently fast allocator. Percentiles and histograms make tails visible.
