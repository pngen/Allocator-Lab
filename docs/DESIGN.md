# Design

Allocator Lab separates experiment definition, strategy interfaces, workload generation, trace capture/replay, measurement, comparison, and reporting into coherent modules, all under the `allocator_lab` namespace.

## Ownership boundary

Allocator Lab owns:

- allocator experiment definitions and strategy interfaces
- allocation/free/reallocation mechanics and instrumentation
- synthetic and trace-driven workloads and deterministic randomization
- latency, throughput, fragmentation, reuse, contention, and capacity measurement
- trace capture and deterministic replay
- comparison, aggregation, statistical summaries, and reports
- capability discovery and explainability

It does not act as an authoritative allocator for an application runtime.

## Value types

Stable `AllocatorId` and `AllocationId` integers are used as identity. Raw pointers are not used as stable experiment identity, so traces remain meaningful across runs and stale or foreign handles can be rejected.

## Strategy interface

`AllocatorStrategy` exposes `allocate`, `free`, `reallocate`, `query`, `trim`, `reset`, `statistics`, and `capabilities`. Unsupported operations return structured errors. `shutdown()` guarantees cleanup of owned backing (CUDA, pinned, shared, mapped, pool backing).

## Registry

`AllocatorRegistry` maps stable ids to strategy instances, supporting registration by fresh or explicit id. Reads take a shared lock and writes take a unique lock; no user callbacks run while a lock is held.

## Alignment and correctness

Alignment is validated (power-of-two, non-zero) and honored up to each backend's declared maximum. Backing allocated with `_aligned_malloc` must be freed with `_aligned_free`; this is enforced by internal tests that would otherwise corrupt the heap.

## Reentrancy / deadlock audit

Internal locks are never held while invoking a user callback (events) or while joining a worker that needs state guarded by the same lock. Allocators guard their internal state with a single mutex and never re-enter the same lock.
