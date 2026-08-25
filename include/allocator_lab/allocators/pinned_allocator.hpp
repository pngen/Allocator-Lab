#pragma once

// Allocator Lab 1.0.0
// Pinned (page-locked) host allocator: cudaMallocHost / cudaFreeHost.
//
// Pinned memory is a constrained system resource; keep allocations bounded.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "allocator_lab/detail/allocator_base.hpp"
#include "allocator_lab/detail/platform.hpp"
#include "allocator_lab/config.hpp"

#ifdef ALLOCATOR_LAB_HAS_CUDA
  #include <cuda_runtime.h>
#endif

namespace allocator_lab {

class PinnedAllocator : public detail::AllocatorBase {
public:
    explicit PinnedAllocator(std::uint64_t budget_bytes = 256ull * 1024 * 1024, std::string name = "pinned", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::Pinned), budget_bytes_(budget_bytes) {
        AllocatorCapabilities c;
        c.domains = { MemoryDomain::Pinned };
        c.is_thread_safe = true;
        c.max_alignment = 4096;
        c.max_allocation_size = budget_bytes_;
#ifdef ALLOCATOR_LAB_HAS_CUDA
        c.supports_allocate = true; c.supports_free = true; c.supports_query = true;
        c.notes = "cudaMallocHost/cudaFreeHost (page-locked)";
#else
        c.supports_allocate = false; c.supports_free = false; c.supports_query = true;
        c.notes = "pinned host memory requires CUDA (not available in this build)";
#endif
        set_capabilities(std::move(c));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
#ifdef ALLOCATOR_LAB_HAS_CUDA
        if (req.size > budget_bytes_) { r.error = make_error(ErrorCode::size_too_large, "pinned budget exceeded"); return r; }
        if (allocated_bytes_ + req.size > budget_bytes_) { r.error = make_error(ErrorCode::capacity_exceeded, "pinned budget reached"); ++failures_; return r; }
        std::size_t align = req.alignment ? req.alignment : 256;
        if (!is_power_of_two(align) || align > 4096) { r.error = make_error(ErrorCode::alignment_unsupported, "pinned supports up to 4096-byte alignment"); return r; }
        const std::size_t sz = req.size == 0 ? 1 : req.size;
        void* p = nullptr;
        cudaError_t e = cudaMallocHost(&p, sz);
        if (e != cudaSuccess) { r.error = make_error(cuda_to_error(e), cuda_error_message(e)); return r; }
        if (p && any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(p, req.size);
        AllocationId id = ++next_id_;
        records_.emplace(id, Record{ p, sz });
        allocated_bytes_ += sz;
        r.error = Error{}; r.id = id; r.address = p; r.size = req.size; r.alignment = 256;
        ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
        live_bytes_ += sz; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        requested_bytes_ += req.size; granted_bytes_ += sz;
        return r;
#else
        (void)req;
        r.error = make_error(ErrorCode::no_backend, "CUDA not available");
        return r;
#endif
    }

    Error free(AllocationHandle handle) override {
        std::lock_guard lock(mutex_);
#ifdef ALLOCATOR_LAB_HAS_CUDA
        auto it = records_.find(handle);
        if (it == records_.end()) return make_error(ErrorCode::invalid_handle, "free of unknown/foreign handle");
        cudaError_t e = cudaFreeHost(it->second.ptr);
        if (e != cudaSuccess) { return make_error(cuda_to_error(e), cuda_error_message(e)); }
        allocated_bytes_ -= (it->second.size <= allocated_bytes_ ? it->second.size : allocated_bytes_);
        if (live_bytes_ >= it->second.size) live_bytes_ -= it->second.size;
        records_.erase(it);
        if (live_count_) --live_count_;
        return Error{};
#else
        (void)handle;
        return make_error(ErrorCode::no_backend, "CUDA not available");
#endif
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = records_.find(handle);
        if (it == records_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        out.id = handle; out.size = it->second.size; out.granted_size = it->second.size;
        out.alignment = 256; out.domain = MemoryDomain::Pinned; out.address = it->second.ptr; out.live = true;
        return Error{};
    }

    AllocatorStatistics statistics() const override {
        std::lock_guard lock(mutex_);
        AllocatorStatistics s;
        s.allocator_id = id_; s.name = name_;
        s.live.live_count = live_count_; s.live.live_bytes = live_bytes_;
        s.live.peak_live_count = peak_live_; s.live.peak_live_bytes = peak_bytes_;
        s.live.reserved = allocated_bytes_; s.live.committed = allocated_bytes_;
        s.accounting.total_requested_bytes = requested_bytes_;
        s.accounting.total_granted_bytes = granted_bytes_;
        s.fragmentation.internal_observable = false; s.fragmentation.external_observable = false;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
#ifdef ALLOCATOR_LAB_HAS_CUDA
        for (auto& kv : records_) { cudaError_t e = cudaFreeHost(kv.second.ptr); (void)e; }
#endif
        records_.clear(); allocated_bytes_ = 0; live_count_ = 0; live_bytes_ = 0;
    }

    std::uint64_t budget_bytes() const { return budget_bytes_; }

private:
    struct Record { void* ptr; std::size_t size; };
#ifdef ALLOCATOR_LAB_HAS_CUDA
    static ErrorCode cuda_to_error(cudaError_t e) {
        switch (e) {
            case cudaSuccess: return ErrorCode::none;
            case cudaErrorMemoryAllocation: return ErrorCode::out_of_memory;
            case cudaErrorInvalidValue: return ErrorCode::invalid_argument;
            case cudaErrorInvalidDevicePointer: return ErrorCode::invalid_handle;
            case cudaErrorNotSupported: return ErrorCode::not_supported;
            default: return ErrorCode::operation_failed;
        }
    }
    static const char* cuda_error_message(cudaError_t e) { return cudaGetErrorName(e); }
#endif
    mutable std::mutex mutex_;
    std::unordered_map<AllocationId, Record> records_;
    AllocationId next_id_ = 0;
    std::uint64_t allocated_bytes_ = 0;
    std::uint64_t budget_bytes_ = 0;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0, granted_bytes_ = 0;
    std::uint64_t success_ = 0, failures_ = 0;
    Bounds bounds_;
};

} // namespace allocator_lab
