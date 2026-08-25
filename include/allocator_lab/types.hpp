#pragma once

// Allocator Lab 1.0.0
// Core value types and enums for the allocator experimentation laboratory.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>
#include <array>

#include "allocator_lab/error.hpp"

namespace allocator_lab {

// ---------------------------------------------------------------------------
// Stable identities
//
// These are process-local integer handles used as experiment and allocation
// identity. Raw pointers are deliberately NOT used as stable experiment
// identity: a pointer can be reused, move, or be unmapped, so it cannot
// describe an allocation reproducibly across a trace replay or a report.
// ---------------------------------------------------------------------------

/// Stable handle identifying a registered allocator strategy.
using AllocatorId = std::uint64_t;

/// Stable handle identifying a live logical allocation inside an allocator.
/// This is the identity used for free/reallocate/query. It does NOT encode a
/// pointer; the allocator keeps the address<->id mapping internally.
using AllocationId = std::uint64_t;

/// Stable handle identifying an event emitted by the lab.
using EventId = std::uint64_t;

/// Index identifying a worker/thread participating in an experiment.
using WorkerId = std::uint32_t;

/// Stable id for an experiment instance.
using ExperimentId = std::uint64_t;

/// Stable id for an experiment snapshot.
using SnapshotId = std::uint64_t;

// ---------------------------------------------------------------------------
// Allocator kinds and memory domains
// ---------------------------------------------------------------------------

/// The strategy family an allocator belongs to. Used for documentation, report
/// grouping and capability display; it is not used to fake behavior.
enum class AllocatorKind : std::uint16_t {
    System = 0,          ///< Direct OS/system allocation.
    Aligned = 1,         ///< Explicit alignment support over system memory.
    FixedPool = 2,       ///< Fixed-size object pool.
    SizeClassPool = 3,   ///< Multiple bounded size classes.
    Slab = 4,            ///< Page/slab-backed fixed category allocation.
    Arena = 5,           ///< Monotonic bump allocation with reset semantics.
    FreeList = 6,        ///< Reusable variable-size blocks.
    Buddy = 7,           ///< Power-of-two split/coalesce allocator.
    SegregatedFit = 8,   ///< Bounded segregated free lists.
    Pinned = 9,          ///< Page-locked host allocation.
    CudaDirect = 10,     ///< cudaMalloc/cudaFree.
    CudaPool = 11,       ///< cudaMallocAsync / CUDA memory pools.
    Shared = 12,         ///< Interprocess shared memory.
    Mapped = 13,         ///< File-backed / memory-mapped backing.
    Custom = 14,         ///< Caller-provided strategy.
};

/// Which physical/virtual memory domain an allocation targets.
enum class MemoryDomain : std::uint16_t {
    Host = 0,        ///< Ordinary host memory.
    Aligned = 1,     ///< Explicitly aligned host memory.
    Pinned = 2,      ///< Page-locked host memory.
    Device = 3,      ///< Accelerator-local device memory.
    Shared = 4,      ///< Interprocess shared memory.
    Mapped = 5,      ///< File-backed / memory-mapped backing.
};

/// Bitmask of allocation flags.
enum class AllocationFlags : std::uint16_t {
    None = 0,
    ZeroFill = 1 << 0,        ///< Zero memory before handing out.
    PoisonAlloc = 1 << 1,     ///< Fill with poison pattern on allocate (debug).
    PoisonFree = 1 << 2,      ///< Fill with poison pattern on free (debug).
};

constexpr AllocationFlags operator|(AllocationFlags a, AllocationFlags b) noexcept {
    return static_cast<AllocationFlags>(static_cast<std::uint16_t>(a) |
                                        static_cast<std::uint16_t>(b));
}
constexpr AllocationFlags operator&(AllocationFlags a, AllocationFlags b) noexcept {
    return static_cast<AllocationFlags>(static_cast<std::uint16_t>(a) &
                                        static_cast<std::uint16_t>(b));
}
constexpr bool any(AllocationFlags a) noexcept { return static_cast<std::uint16_t>(a) != 0; }
constexpr bool has(AllocationFlags a, AllocationFlags f) noexcept {
    return (static_cast<std::uint16_t>(a) & static_cast<std::uint16_t>(f)) != 0;
}

/// Explicit zeroing / scrubbing policy for allocators that reuse backing.
enum class ZeroingPolicy : std::uint8_t {
    Never = 0,
    OnAlloc = 1,
    OnFree = 2,
    OnReuse = 3,
    Always = 4,
};

/// Trace / event operation types.
enum class TraceOperationType : std::uint16_t {
    Allocate = 0,
    Free = 1,
    Reallocate = 2,
    Trim = 3,
    Reset = 4,
    Barrier = 5,
    PhaseMarker = 6,
};

/// Determines how reallocation behaves when growing or moving.
enum class ReallocSemantics : std::uint8_t {
    /// Grow/shrink may move; contents preserved on success. Dominant default.
    PreserveOnSuccess,
    /// A higher-level guarantee that the caller keeps the old block alive on
    /// failure (always true for this lab's returned-id model).
    KeepOldOnFailure,
};

// ---------------------------------------------------------------------------
// Allocation requests / results
// ---------------------------------------------------------------------------

/// A single allocation request presented to a strategy.
struct AllocationRequest {
    std::size_t size = 0;
    /// Requested alignment. 0 means "use the strategy's natural/default".
    std::size_t alignment = 0;
    MemoryDomain domain = MemoryDomain::Host;
    AllocationFlags flags = AllocationFlags::None;
    /// Optional caller tag, e.g. a workload class id. Not interpreted by the
    /// strategy beyond being carried through for reporting.
    std::uint32_t tag = 0;
};

/// The outcome of an allocate / reallocate operation.
struct AllocationResult {
    Error error;                 ///< Empty (ok) on success.
    AllocationId id = 0;         ///< Stable logical id, valid only on success.
    void* address = nullptr;     ///< Raw address for the caller to use.
    std::size_t size = 0;        ///< Granted size
    std::size_t alignment = 0;   ///< Effective alignment
    bool reused_backing = false; ///< Whether the returned block was reused backing.
};

/// A stable handle used to free / query an allocation.
using AllocationHandle = AllocationId;

/// Information returned by query(handle).
struct AllocationInfo {
    AllocationId id = 0;
    std::size_t size = 0;
    std::size_t granted_size = 0;
    std::size_t alignment = 0;
    MemoryDomain domain = MemoryDomain::Host;
    void* address = nullptr;
    bool live = false;
    std::uint32_t tag = 0;
};

/// A record describing one allocation that is (or was) backed by a strategy.
struct AllocationRecord {
    AllocationId id = 0;
    std::size_t requested_size = 0;
    std::size_t granted_size = 0;
    std::size_t alignment = 0;
    MemoryDomain domain = MemoryDomain::Host;
    std::uint32_t tag = 0;
    WorkerId worker = 0;
    bool live = false;
    std::uint64_t allocate_step = 0;
    std::uint64_t free_step = 0;
};

} // namespace allocator_lab
