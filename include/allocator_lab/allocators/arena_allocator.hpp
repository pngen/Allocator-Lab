#pragma once

// Allocator Lab 1.0.0
// Arena / bump allocator: monotonic allocation with reset semantics.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <mutex>
#include <vector>
#include <utility>

#include "allocator_lab/detail/allocator_base.hpp"
#include "allocator_lab/detail/platform.hpp"
#include "allocator_lab/config.hpp"

namespace allocator_lab {

class ArenaAllocator : public detail::AllocatorBase {
public:
    ArenaAllocator(std::size_t block_size = 4u * 1024 * 1024, std::size_t max_bytes = size_t(-1),
                  std::string name = "arena", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::Arena),
          block_size_(block_size), max_bytes_(max_bytes) {
        AllocatorCapabilities c;
        c.supports_allocate = true;
        c.supports_free = false;            // arena cannot free individual objects
        c.allows_per_object_free = false;
        c.supports_query = true;
        c.supports_reset = true;
        c.supports_trim = false;
        c.supports_alignment = true;
        c.is_thread_safe = true;
        c.max_alignment = 4096;
        c.max_allocation_size = bounds_.max_allocation_size;
        c.domains = { MemoryDomain::Host };
        set_capabilities(std::move(c));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        if (req.size > bounds_.max_allocation_size) { r.error = make_error(ErrorCode::size_too_large, "too large"); return r; }
        std::size_t align = req.alignment ? req.alignment : detail::default_alignment;
        if (!is_power_of_two(align) || align > 4096) { r.error = make_error(ErrorCode::alignment_unsupported, "alignment unsupported"); return r; }
        std::size_t padded = round_up_pow2_padded(req.size == 0 ? 1 : req.size, align);
        if (current_ + padded > cap_) {
            if (!ensure_block(padded)) { r.error = make_error(ErrorCode::out_of_memory, "arena could not grow"); ++failures_; return r; }
        }
        void* p = base_ + current_;
        current_ += padded;
        if (current_ > high_ ) high_ = current_;
        if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(p, req.size);
        if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(p, req.size, detail::kPoisonAllocByte);
        AllocationId id = ++next_id_;
        live_.emplace(id, p);
        live_size_.emplace(id, req.size);
        r.error = Error{}; r.id = id; r.address = p; r.size = req.size; r.alignment = align; r.reused_backing = current_ > 0;
        ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
        live_bytes_ += req.size; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        requested_bytes_ += req.size; granted_bytes_ += padded;
        return r;
    }

    Error free(AllocationHandle handle) override {
        (void)handle;
        return make_error(ErrorCode::not_supported, "arena does not support per-object free");
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        out.id = handle; out.size = live_size_.count(handle) ? live_size_.at(handle) : 0;
        out.address = it->second; out.live = true;
        return Error{};
    }

    Error reset() override {
        std::lock_guard lock(mutex_);
        // Reset releases all backing and starts over. Not safe to call while any
        // live allocations are still in use; treated as a barrier / reinitialization.
        for (auto* b : blocks_) detail::aligned_free(b);
        blocks_.clear();
        base_ = nullptr; current_ = 0; cap_ = 0; high_ = 0;
        live_.clear(); live_size_.clear();
        live_count_ = 0; live_bytes_ = 0;
        return Error{};
    }

    AllocatorStatistics statistics() const override {
        std::lock_guard lock(mutex_);
        AllocatorStatistics s;
        s.allocator_id = id_; s.name = name_;
        s.live.live_count = live_count_; s.live.live_bytes = live_bytes_;
        s.live.peak_live_count = peak_live_; s.live.peak_live_bytes = peak_bytes_;
        s.live.reserved = cap_; s.live.committed = cap_; s.live.peak_reserved = peak_cap_ > cap_ ? peak_cap_ : cap_;
        s.accounting.total_requested_bytes = requested_bytes_;
        s.accounting.total_granted_bytes = granted_bytes_;
        s.accounting.internal_waste = granted_bytes_ - requested_bytes_;
        s.accounting.alignment_waste = alignment_waste_;
        s.reuse.fresh_allocations = growth_events_;
        s.reuse.backing_retained_after_workload = cap_;
        s.fragmentation.internal_observable = true; s.fragmentation.external_observable = false;
        s.fragmentation.internal_fragmentation = cap_ > 0 ? 1.0 - (double)live_bytes_ / (double)cap_ : 0.0;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        for (auto* b : blocks_) detail::aligned_free(b);
        blocks_.clear(); live_.clear(); live_size_.clear();
        base_ = nullptr; current_ = 0; cap_ = 0;
        live_count_ = 0; live_bytes_ = 0;
    }

private:
    static std::size_t round_up_pow2_padded(std::size_t v, std::size_t align) {
        std::size_t m = align - 1;
        return (v + m) & ~m;
    }
    bool ensure_block(std::size_t need) {
        std::size_t sz = block_size_;
        if (sz < need) sz = round_up_pow2_padded(need, 4096);
        if (cap_ + sz > max_bytes_) return false;
        void* b = detail::aligned_alloc(sz, 4096);
        if (!b) return false;
        blocks_.push_back(b);
        base_ = static_cast<char*>(b);
        current_ = 0; cap_ = sz;
        ++growth_events_;
        if (cap_ > peak_cap_) peak_cap_ = cap_;
        return true;
    }

    mutable std::mutex mutex_;
    std::vector<void*> blocks_;
    std::unordered_map<AllocationId, void*> live_;
    std::unordered_map<AllocationId, std::size_t> live_size_;
    AllocationId next_id_ = 0;
    char* base_ = nullptr;
    std::size_t current_ = 0, cap_ = 0, high_ = 0;
    std::size_t block_size_ = 0, max_bytes_ = 0;
    std::size_t growth_events_ = 0;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0, granted_bytes_ = 0;
    std::uint64_t alignment_waste_ = 0;
    std::uint64_t success_ = 0, failures_ = 0;
    std::uint64_t peak_cap_ = 0;
    Bounds bounds_;
};

} // namespace allocator_lab
