#pragma once

// Allocator Lab 1.0.0
// The stable allocator strategy abstraction.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstddef>
#include <string>

#include "allocator_lab/types.hpp"
#include "allocator_lab/error.hpp"
#include "allocator_lab/capabilities.hpp"
#include "allocator_lab/metrics.hpp"

namespace allocator_lab {

/// Stable human readable name for an allocator kind.
inline const char* allocator_kind_name(AllocatorKind kind) noexcept {
    switch (kind) {
        case AllocatorKind::System: return "system";
        case AllocatorKind::Aligned: return "aligned";
        case AllocatorKind::FixedPool: return "fixed_pool";
        case AllocatorKind::SizeClassPool: return "size_class_pool";
        case AllocatorKind::Slab: return "slab";
        case AllocatorKind::Arena: return "arena";
        case AllocatorKind::FreeList: return "free_list";
        case AllocatorKind::Buddy: return "buddy";
        case AllocatorKind::SegregatedFit: return "segregated_fit";
        case AllocatorKind::Pinned: return "pinned";
        case AllocatorKind::CudaDirect: return "cuda_direct";
        case AllocatorKind::CudaPool: return "cuda_pool";
        case AllocatorKind::Shared: return "shared";
        case AllocatorKind::Mapped: return "mapped";
        case AllocatorKind::Custom: return "custom";
    }
    return "unknown";
}

/// The allocator strategy contract.
///
/// Implementations map a stable AllocationId to their internal address. Raw
/// pointers are never used as the caller-facing identity, so a trace can be
/// replayed deterministically and a query/free can reject foreign or stale ids.
///
/// Operations the strategy does not implement MUST return a structured
/// "not_supported" / "invalid_handle" error. The lab never fabricates success.
class AllocatorStrategy {
public:
    virtual ~AllocatorStrategy() = default;

    /// Stable id (assigned by the registry, never reused during a run).
    virtual AllocatorId id() const noexcept = 0;

    /// Accept an assigned id. The registry calls this right after construction
    /// when a strategy does not self-assign a non-zero id. Default: no-op.
    virtual void set_id(AllocatorId id) noexcept { (void)id; }

    virtual const std::string& name() const noexcept = 0;
    virtual AllocatorKind kind() const noexcept = 0;

    /// Honest capability report.
    virtual const AllocatorCapabilities& capabilities() const noexcept = 0;

    /// Short human description.
    virtual std::string describe() const { return name(); }

    /// Allocate. Returns a structured result. On failure the error is set and
    /// no id/address is published. On success id and address are valid.
    virtual AllocationResult allocate(const AllocationRequest& req) = 0;

    /// Free a live allocation. Returns a structured error on failure.
    virtual Error free(AllocationHandle handle) = 0;

    /// Reallocate. Default semantic: preserve the original allocation and its
    /// contents on failure; on success the handle remains valid (or is updated
    /// per the returned result). If the default is overridden, the semantic must
    /// be documented.
    virtual AllocationResult reallocate(AllocationHandle handle, const AllocationRequest& req) {
        (void)handle; (void)req;
        return AllocationResult{make_error(ErrorCode::not_supported, "reallocation not supported"),
                                0, nullptr, 0, 0, false};
    }

    /// Query an allocation.
    virtual Error query(AllocationHandle handle, AllocationInfo& out) = 0;

    // --- Optional memory-management ops (default: unsupported) ---
    virtual Error trim() { return make_error(ErrorCode::not_supported, "trim not supported"); }
    virtual Error reset() { return make_error(ErrorCode::not_supported, "reset not supported"); }
    virtual Error reserve(std::size_t bytes) {
        (void)bytes; return make_error(ErrorCode::not_supported, "reserve not supported");
    }
    virtual Error commit(void* p, std::size_t bytes) {
        (void)p; (void)bytes; return make_error(ErrorCode::not_supported, "commit not supported");
    }
    virtual Error decommit(void* p, std::size_t bytes) {
        (void)p; (void)bytes; return make_error(ErrorCode::not_supported, "decommit not supported");
    }
    virtual Error prewarm() { return make_error(ErrorCode::not_supported, "prewarm not supported"); }
    virtual Error purge() { return make_error(ErrorCode::not_supported, "purge not supported"); }

    /// Snapshot strategy statistics (accounting, fragmentation, reuse, live).
    virtual AllocatorStatistics statistics() const = 0;

    /// Guaranteed cleanup of owned backing (CUDA, pinned, shared, mapped, pool
    /// backing). After a controlled run the lab checks that owned resources
    /// return to a documented baseline. This is called by the runner at the end
    /// of an experiment.
    virtual void shutdown() noexcept = 0;
};

} // namespace allocator_lab
