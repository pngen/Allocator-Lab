#pragma once

// Allocator Lab 1.0.0
// CUDA pool / async allocator: cudaMallocAsync / cudaFreeAsync.
//
// Completed-work semantics: allocation is enqueued on a stream. The lab's
// benchmarks synchronize the stream before reading completed latencies and
// throughput, so numbers represent completed work, never merely submitted work.
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

class CudaPoolAllocator : public detail::AllocatorBase {
public:
    explicit CudaPoolAllocator(std::string name = "cuda_pool", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::CudaPool) {
        AllocatorCapabilities c;
        c.domains = { MemoryDomain::Device };
        c.is_thread_safe = true;
        c.max_alignment = 256;
        c.max_allocation_size = bounds_.max_allocation_size;
#ifdef ALLOCATOR_LAB_HAS_CUDA
        c.supports_allocate = true; c.supports_free = true; c.supports_query = true;
        c.supports_trim = true;
        c.is_experimental = true;
        c.notes = "cudaMallocAsync/cudaFreeAsync; completed work must be synchronized";
#else
        c.supports_allocate = false; c.supports_free = false; c.supports_query = true;
        c.notes = "CUDA not available in this build; backend reports no_backend";
#endif
        set_capabilities(std::move(c));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
#ifdef ALLOCATOR_LAB_HAS_CUDA
        if (req.size > bounds_.max_allocation_size) { r.error = make_error(ErrorCode::size_too_large, "too large"); return r; }
        std::size_t align = req.alignment ? req.alignment : 256;
        if (!is_power_of_two(align) || align > 256) { r.error = make_error(ErrorCode::alignment_unsupported, "CUDA async supports up to 256-byte alignment"); return r; }
        const std::size_t sz = req.size == 0 ? 1 : req.size;
        void* p = nullptr;
        cudaError_t e = cudaMallocAsync(&p, sz, stream_);
        if (e != cudaSuccess) { r.error = make_error(cuda_to_error(e), cuda_error_message(e)); return r; }
        AllocationId id = ++next_id_;
        records_.emplace(id, Record{ p, sz });
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
        cudaError_t e = cudaFreeAsync(it->second.ptr, stream_);
        if (e != cudaSuccess) { return make_error(cuda_to_error(e), cuda_error_message(e)); }
        if (live_bytes_ >= it->second.size) live_bytes_ -= it->second.size;
        records_.erase(it);
        if (live_count_) --live_count_;
        ++reuse_count_;
        return Error{};
#else
        (void)handle;
        return make_error(ErrorCode::no_backend, "CUDA not available");
#endif
    }

    Error trim() override {
        std::lock_guard lock(mutex_);
#ifdef ALLOCATOR_LAB_HAS_CUDA
        cudaDeviceSynchronize();
#endif
        return Error{};
    }

    /// Synchronize the stream so completed work is observable. Call before
    /// reading completed latencies. Returns a structured error on CUDA failure.
    Error sync() {
#ifdef ALLOCATOR_LAB_HAS_CUDA
        cudaError_t e = cudaStreamSynchronize(stream_);
        return e == cudaSuccess ? Error{} : make_error(cuda_to_error(e), cuda_error_message(e));
#else
        return make_error(ErrorCode::no_backend, "CUDA not available");
#endif
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = records_.find(handle);
        if (it == records_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        out.id = handle; out.size = it->second.size; out.granted_size = it->second.size;
        out.alignment = 256; out.domain = MemoryDomain::Device; out.address = it->second.ptr; out.live = true;
        return Error{};
    }

    AllocatorStatistics statistics() const override {
        std::lock_guard lock(mutex_);
        AllocatorStatistics s;
        s.allocator_id = id_; s.name = name_;
        s.live.live_count = live_count_; s.live.live_bytes = live_bytes_;
        s.live.peak_live_count = peak_live_; s.live.peak_live_bytes = peak_bytes_;
        s.accounting.total_requested_bytes = requested_bytes_;
        s.accounting.total_granted_bytes = granted_bytes_;
        s.fragmentation.internal_observable = false; s.fragmentation.external_observable = false;
        s.reuse.reuse_count = reuse_count_;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
#ifdef ALLOCATOR_LAB_HAS_CUDA
        for (auto& kv : records_) { cudaError_t e = cudaFreeAsync(kv.second.ptr, stream_); (void)e; }
        cudaDeviceSynchronize();
#endif
        records_.clear(); live_count_ = 0; live_bytes_ = 0;
    }

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
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0, granted_bytes_ = 0;
    std::uint64_t success_ = 0, failures_ = 0, reuse_count_ = 0;
#ifdef ALLOCATOR_LAB_HAS_CUDA
    cudaStream_t stream_ = nullptr;
#endif
    Bounds bounds_;
};

} // namespace allocator_lab
