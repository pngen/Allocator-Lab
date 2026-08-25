# Hardening

Allocator Lab is tested adversarially.

## Covered

integer and size_t overflow, multiplication overflow, alignment overflow/invalid alignment, double free, use after free, stale handle, foreign allocator handle, buffer overrun, freelist corruption, split/coalesce and buddy merge errors, pool bucket mismatch, backing-size mismatch, reserved/committed accounting errors, leaks after error and after shutdown, race conditions, lock inversion/reentrancy, missed wakeups, concurrent trim/reset races, trace nondeterminism and corruption, malformed report input, nonsensical benchmark units, submit-only measurement, integer-to-double precision loss, negative/invalid serialized values, cleanup failures, CUDA runtime lifetime problems, mmap lifetime problems, and shared-memory stale names.

## Self-deadlock audit

No path acquires a read lock then the same lock for writing; no write guard is held while code re-reads or re-writes the same lock; no observer/event callback runs while an internal lock is held; workers are joined without holding state they need to exit; condition-variable waits do not hold unrelated producer state; size-class/global lock nesting is consistent; trim/reset do not run in an order opposite the normal allocation/free path.

## Memory-safety invariants

Double free, use after free, duplicate live ids, stale handle reuse, wrong-size bookkeeping, wrong alignment, heap overrun, pool backing overflow, freelist corruption, buddy coalescing corruption, size-class mismatch, lost/leaked backing, negative accounting, wraparound, and foreign-handle acceptance are all checked.

## Allocation/free pairing

Backing allocated with `_aligned_malloc` is freed with `_aligned_free`. This is a real, previously-found defect with regression coverage; it would otherwise corrupt the heap.

## Budget

CUDA, pinned, and mappped allocations are bounded. Physical VRAM, pinned RAM, and system RAM are never driven near exhaustion merely to prove failure behavior.
