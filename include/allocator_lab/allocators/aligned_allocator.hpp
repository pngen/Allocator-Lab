#pragma once

// Allocator Lab 1.0.0
// Explicitly aligned allocator over system memory.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "allocator_lab/detail/allocator_base.hpp"
#include "allocator_lab/detail/platform.hpp"
#include "allocator_lab/config.hpp"

namespace allocator_lab {

class AlignedAllocator : public detail::AllocatorBase {
public:
    explicit AlignedAllocator(std::size_t max_align = 4096, std::string name = "aligned", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::Aligned), max_align_(max_align) {
        AllocatorCapabilities c;
        c.supports_allocate = true;
        c.supports_free = true;
        c.supports_reallocate = true;
        c.supports_query = true;
        c.supports_alignment = true;
        c.is_thread_safe = true;
        c.max_alignment = max_align;
        c.max_allocation_size = bounds_.max_allocation_size;
        c.domains = { MemoryDomain::Host, MemoryDomain::Aligned };
        set_capabilities(std::move(c));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        if (req.size > bounds_.max_allocation_size) { r.error = make_error(ErrorCode::size_too_large, "too large"); return r; }
        std::size_t align = req.alignment ? req.alignment : detail::default_alignment;
        if (!is_power_of_two(align)) { r.error = make_error(ErrorCode::invalid_alignment, "alignment must be power of two"); return r; }
        if (align > max_align_) { r.error = make_error(ErrorCode::alignment_unsupported, "alignment exceeds max"); return r; }
        void* p = detail::aligned_alloc(req.size == 0 ? 1 : req.size, align);
        if (!p) { r.error = make_error(ErrorCode::out_of_memory, "aligned allocation failed"); return r; }
        if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(p, req.size);
        if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(p, req.size, detail::kPoisonAllocByte);
        AllocationId id = ++next_id_;
        records_.emplace(id, Record{ p, req.size, align, req.domain, req.tag });
        r.error = Error{}; r.id = id; r.address = p; r.size = req.size; r.alignment = align; r.reused_backing = false;
        requested_bytes_ += req.size; granted_bytes_ += req.size; live_bytes_ += req.size; ++success_;
        if (records_.size() > peak_live_) peak_live_ = static_cast<std::uint64_t>(records_.size());
        if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        return r;
    }

    Error free(AllocationHandle handle) override {
        std::lock_guard lock(mutex_);
        auto it = records_.find(handle);
        if (it == records_.end()) return make_error(ErrorCode::invalid_handle, "free of unknown/foreign handle");
        if (poison_on_free_) detail::fill_poison(it->second.ptr, it->second.size, detail::kPoisonFreeByte);
        detail::aligned_free(it->second.ptr);
        if (live_bytes_ >= it->second.size) live_bytes_ -= it->second.size;
        records_.erase(it);
        return Error{};
    }

    AllocationResult reallocate(AllocationHandle handle, const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        auto it = records_.find(handle);
        if (it == records_.end()) { r.error = make_error(ErrorCode::invalid_handle, "realloc of unknown handle"); return r; }
        if (req.size > bounds_.max_allocation_size) { r.error = make_error(ErrorCode::size_too_large, "too large"); return r; }
        std::size_t align = req.alignment ? req.alignment : it->second.alignment;
        if (align > max_align_) { r.error = make_error(ErrorCode::alignment_unsupported, "alignment exceeds max"); return r; }
        void* np = detail::aligned_alloc(req.size == 0 ? 1 : req.size, align);
        if (!np) { r.error = make_error(ErrorCode::out_of_memory, "realloc failed; original preserved"); return r; }
        std::size_t copy = it->second.size < req.size ? it->second.size : req.size;
        if (copy) std::memcpy(np, it->second.ptr, copy);
        detail::aligned_free(it->second.ptr);
        if (live_bytes_ >= it->second.size) live_bytes_ -= it->second.size;
        live_bytes_ += req.size;
        granted_bytes_ += (req.size > it->second.size ? req.size - it->second.size : 0);
        requested_bytes_ += req.size;
        if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        it->second.ptr = np; it->second.size = req.size; it->second.alignment = align; it->second.tag = req.tag;
        r.error = Error{}; r.id = handle; r.address = np; r.size = req.size; r.alignment = align; r.reused_backing = false;
        return r;
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = records_.find(handle);
        if (it == records_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        out.id = handle; out.size = it->second.size; out.granted_size = it->second.size;
        out.alignment = it->second.alignment; out.domain = it->second.domain; out.address = it->second.ptr; out.live = true; out.tag = it->second.tag;
        return Error{};
    }

    AllocatorStatistics statistics() const override {
        std::lock_guard lock(mutex_);
        AllocatorStatistics s;
        s.allocator_id = id_; s.name = name_;
        s.live.live_count = static_cast<std::uint64_t>(records_.size());
        s.live.live_bytes = live_bytes_; s.live.peak_live_count = peak_live_; s.live.peak_live_bytes = peak_bytes_;
        s.accounting.total_requested_bytes = requested_bytes_;
        s.accounting.total_granted_bytes = granted_bytes_;
        s.accounting.internal_waste = granted_bytes_ - requested_bytes_;
        s.fragmentation.internal_observable = false; s.fragmentation.external_observable = false;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        for (auto& kv : records_) detail::aligned_free(kv.second.ptr);
        records_.clear(); live_bytes_ = 0; granted_bytes_ = 0; requested_bytes_ = 0; peak_live_ = 0; peak_bytes_ = 0;
    }

private:
    struct Record { void* ptr; std::size_t size; std::size_t alignment; MemoryDomain domain; std::uint32_t tag; };
    mutable std::mutex mutex_;
    std::unordered_map<AllocationId, Record> records_;
    AllocationId next_id_ = 0;
    std::size_t max_align_ = 4096;
    std::uint64_t live_bytes_ = 0, requested_bytes_ = 0, granted_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0, success_ = 0, failures_ = 0;
    bool poison_on_free_ = false;
    Bounds bounds_;
};

} // namespace allocator_lab
