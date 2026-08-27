# Allocator Lab

**Open-source, vendor-neutral laboratory for designing, benchmarking, stress-testing, and comparing memory allocators across host, pinned, accelerator, shared, and persistent memory domains.**

Allocator Lab 1.0.0

Core systems question: *How should memory allocation strategies be measured, compared, stressed, and understood before one of them is trusted inside serious AI infrastructure?*

## What Allocator Lab is

Allocator Lab measures allocator behavior: latency, throughput, fragmentation, reuse, contention, capacity, failure handling, and cleanup under deterministic workloads.

It turns allocator behavior into something explicit, measurable, reproducible, adversarially testable, and inspectable. Engineers use the laboratory to:

- implement allocator strategies
- configure allocator policies
- construct repeatable workloads
- measure allocation, free, and reallocation behavior
- compare strategies fairly on identical traces
- expose fragmentation, contention, latency tails, cleanup failures, and accounting mistakes
- replay identical allocation traces against many allocators
- stress failure paths and capacity exhaustion
- generate machine-readable reports and explanations

## Why it is a Lab

Allocator Lab is deliberately a measurement and experimentation system rather than a production allocation authority. It owns allocator experimentation, measurement, replay, comparison, and adversarial stress. It evaluates allocation strategies; it is not an authoritative runtime memory manager.

Several included allocator strategies are intentionally simple controls or baselines. The laboratory makes semantic differences between strategies visible instead of crowning a universal winner.

## The allocator strategy abstraction

All strategies implement a stable interface (`allocator_lab::AllocatorStrategy`):

- `allocate(size, alignment, domain, flags)`
- `free(handle)`
- `reallocate(handle, new_size, alignment)`
- `query(handle)`
- `trim` / `reset` where supported
- `statistics()`
- capability discovery

Raw pointers are never used as stable experiment identity. Allocations are identified by stable logical ids, so traces are replayable and foreign/stale handles are rejected. Unsupported operations return structured errors instead of fabricated success.

## Built-in allocator strategies

- System / direct allocator
- Aligned system allocator
- Fixed-size object pool
- Size-class pool
- Slab-like (page-backed fixed category) allocator
- Arena / bump allocator (no per-object free)
- Free-list allocator (boundary-tag coalescing)
- Buddy-like allocator (power-of-two split/coalesce)
- Segregated-fit experimental allocator (bounded segregated free lists)
- Pinned host allocator (page-locked, where supported)
- CUDA direct allocator (`cudaMalloc`/`cudaFree`)
- CUDA pool / async allocator (`cudaMallocAsync`/`cudaFreeAsync` where supported)
- Shared-memory allocator (real OS file mapping)
- File-backed / mmap-style allocator

Custom strategies can be registered by stable id without rewriting the laboratory.

## Memory domains

Host, aligned host, pinned host, accelerator-local device, shared/interprocess, and file-backed/mapped domains are first-class. Backends that are not implemented in a given build report themselves honestly via capabilities and return `no_backend` rather than pretending support.

## Workloads, traces, and replay

The deterministic workload generator supports fixed, uniform, log-uniform, geometric, bimodal, multimodal, power-law-like, histogram, and caller-provided size distributions; multiple lifetime distributions; burstiness; hot/cold classes; concurrency; phases; and a seed. The same seed reproduces the identical trace.

Traces use a strict, versioned format (human-readable JSON and a compact binary). Malformed data — bad enums, negative sizes, invalid alignment, duplicate live ids, free of nonexistent ids, oversized traces, truncation, and invalid operation ordering — is rejected, never silently coerced. A captured or generated trace replays deterministically against any allocator, which is central to fair comparison.

## Metrics

Allocator Lab records allocation/free/reallocation latency distributions (mean, median, p90/p95/p99/p99.9, max, min), throughput, live and peak state, reserved/committed state, accounting (requested/granted/waste/reuse/fresh), explicit fragmentation metrics, reuse metrics, and tail latency. Where a backend cannot observe internal topology, the corresponding metric is labeled unsupported rather than fabricated.

## Concurrency, capacity, and failure injection

The laboratory is thread-safe; strategies marked single-thread-only reject concurrent use. Contention is measured across 1/2/4/8+ threads. Graceful capacity exhaustion leaves existing allocations valid and accounting consistent. Failure injection covers metadata and backing allocation, pool/arena growth, split/coalesce, alignment, reallocation, CUDA/pinned/shared/mmap operations, and initialization.

## Real-hardware and real-process proofs

- Real host and aligned allocation
- Real pinned host allocation where supported
- Real CUDA allocation/free with observed device memory before/after
- Real two-process shared-memory proof (creator writes, a second OS process opens and verifies)
- Real file-backed/mapped create-map-write-unmap-reopen

## CLI

`allocator-lab` exposes `info`, `allocators`, `capabilities`, `run`, `compare`, `trace-generate`, `trace-capture`, `trace-replay`, `inspect-trace`, `stress`, `fragmentation`, `concurrency`, `cuda`, `pinned`, `shared`, `mmap`, `report`, `explain`, and `benchmark`, with options for allocator, workload, domain, seed, size, distribution, alignment, trace, and output format.

## Install and downstream use

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix <install-dir>
```

Downstream:

```cmake
find_package(allocator_lab CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE AllocatorLab::allocator_lab)
```

## Documentation

See `docs/` for the design, allocator catalog, workload model, metrics, trace format, benchmarks, hardening guide, and portability notes.

## License

Apache License 2.0. Copyright 2026 Summon Software Labs. No telemetry transmission.
