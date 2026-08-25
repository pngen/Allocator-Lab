# Traces

The trace format is versioned (`kTraceVersion = 1`) and strict.

## Operations

`ALLOCATE`, `FREE`, `REALLOCATE`, `TRIM`, `RESET`, `BARRIER`, `PHASE_MARKER`.

## Entry fields

sequence number, logical step, allocation id, size, alignment, domain, worker id, operation type, optional tag, and optional recorded status.

Allocation ids are stable logical ids, never raw addresses, so a trace remains meaningful and replayable across runs.

## Serialization

Two formats are provided: human-readable JSON and a compact binary. Both are versioned.

## Rejection

Malformed enum values, malformed ids, negative or overflow sizes, invalid/zero alignment, non-power-of-two alignment where required, oversized traces/metadata, truncated data, impossible operation ordering, free of a never-allocated id, duplicate live allocation ids, and invalid reallocation targets are rejected. Data is never silently coerced.

## Replay

With an identical trace, seed, and allocator configuration, operation order, requested sizes, alignments, allocation identities, and experiment metadata are deterministic. This is central to fair comparison.
