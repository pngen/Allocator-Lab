#pragma once

// Allocator Lab 1.0.0
// Fixed-size object pool with reuse tracking and bounded growth.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <utility>

#include "allocator_lab/detail/allocator_base.hpp"
#include "allocator_lab/detail/platform.hpp"
#include "allocator_lab/config.hpp"

namespace allocator_lab {

class FixedPool : public detail::AllocatorBase {
public:
    FixedPool(std::size_t object_size, std::size_t capacity, std::size_t alignment = 64,
             std::string name = "fixed_pool", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::FixedPool),
          object_size_(object_size), capacity_(capacity), align_(alignment) {
        slot_stride_ = round_up_pow2(object_size_ > align_ ? object_size_ : align_);
        AllocatorCapabilities c;
        c.supports_allocate = true; c.supports_free = true;
        c.supports_query = true; c.supports_reset = true; c.supports_trim = true;
        c.supports_alignment = true; c.is_thread_safe = true;
        c.max_alignment = align_; c.max_allocation_size = object_size_;
        c.domains = { MemoryDomain::Host };
        c.is_experimental = false;
        set_capabilities(std::move(c));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        if (req.size > object_size_) { r.error = make_error(ErrorCode::size_too_large, "object exceeds pool object_size"); return r; }
        std::size_t want_align = req.alignment ? req.alignment : align_;
        if (!is_power_of_two(want_align) || want_align > align_) { r.error = make_error(ErrorCode::alignment_unsupported, "alignment not supported by pool"); return r; }
        bool grow = free_list_.empty();
        if (grow) {
            if (total_slots_ + block_slots() > capacity_) { r.error = make_error(ErrorCode::capacity_exceeded, "pool capacity reached"); ++failures_; return r; }
            grow_block();
            ++growth_events_;
            ++fresh_allocs_;
        }
        if (free_list_.empty()) { r.error = make_error(ErrorCode::out_of_memory, "pool growth failed"); ++failures_; return r; }
        void* slot = free_list_.back(); free_list_.pop_back();
        bool reused = !grow;
        if (reused) { ++pool_hits_; ++reuse_count_; ++same_size_reuse_; }
        if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(slot, object_size_);
        if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(slot, object_size_, detail::kPoisonAllocByte);
        AllocationId id = ++next_id_;
        live_.emplace(id, slot);
        r.error = Error{}; r.id = id; r.address = slot; r.size = object_size_; r.alignment = align_; r.reused_backing = reused;
        ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
        live_bytes_ += object_size_; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        requested_bytes_ += req.size; granted_bytes_ += object_size_;
        return r;
    }

    Error free(AllocationHandle handle) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "free of unknown/foreign handle");
        if (poison_on_free_) detail::fill_poison(it->second, object_size_, detail::kPoisonFreeByte);
        free_list_.push_back(it->second);
        live_.erase(it);
        if (live_count_) --live_count_; if (live_bytes_ >= object_size_) live_bytes_ -= object_size_;
        ++same_size_reuse_;
        return Error{};
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        out.id = handle; out.size = object_size_; out.granted_size = object_size_; out.alignment = align_;
        out.domain = MemoryDomain::Host; out.address = it->second; out.live = true;
        return Error{};
    }

    Error reset() override {
        std::lock_guard lock(mutex_);
        free_list_.clear();
        for (auto* b : blocks_) detail::aligned_free(b);
        blocks_.clear(); live_.clear();
        total_slots_ = 0; live_count_ = 0; live_bytes_ = 0;
        return Error{};
    }

    Error trim() override {
        // Trim returns no buffered block; pool keeps its backing by design.
        std::lock_guard lock(mutex_);
        ++trim_count_;
        return Error{};
    }

    AllocatorStatistics statistics() const override {
        std::lock_guard lock(mutex_);
        AllocatorStatistics s;
        s.allocator_id = id_; s.name = name_;
        s.live.live_count = live_count_; s.live.live_bytes = live_bytes_;
        s.live.peak_live_count = peak_live_; s.live.peak_live_bytes = peak_bytes_;
        s.live.reserved = static_cast<std::uint64_t>(blocks_.size()) * block_bytes_;
        s.live.committed = s.live.reserved; s.live.peak_reserved = s.live.reserved; s.live.peak_committed = s.live.reserved;
        s.accounting.total_requested_bytes = requested_bytes_;
        s.accounting.total_granted_bytes = granted_bytes_;
        s.accounting.internal_waste = granted_bytes_ - requested_bytes_;
        s.accounting.reused_bytes = reuse_bytes_; s.accounting.fresh_bytes = fresh_bytes_;
        s.reuse.pool_hits = pool_hits_; s.reuse.fresh_allocations = fresh_allocs_;
        s.reuse.reuse_count = reuse_count_; s.reuse.same_size_reuse = same_size_reuse_;
        s.reuse.trim_recovery_bytes = 0;
        s.reuse.backing_retained_after_workload = static_cast<std::uint64_t>(blocks_.size()) * block_bytes_;
        s.fragmentation.internal_observable = true; s.fragmentation.external_observable = false;
        s.fragmentation.internal_fragmentation = granted_bytes_ > 0 ? 1.0 - (double)requested_bytes_ / (double)granted_bytes_ : 0.0;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        for (auto* b : blocks_) detail::aligned_free(b);
        blocks_.clear(); free_list_.clear(); live_.clear();
        live_count_ = 0; live_bytes_ = 0;
    }

private:
    std::size_t block_slots() const { return block_slots_; }
    void grow_block() {
        std::size_t n = block_slots_;
        std::size_t bytes = slot_stride_ * n;
        if (bytes < align_) bytes = align_;
        void* base = detail::aligned_alloc(bytes, align_);
        if (!base) return;
        blocks_.push_back(base);
        block_bytes_ = bytes;
        char* p = static_cast<char*>(base);
        for (std::size_t i = 0; i < n; ++i) free_list_.push_back(p + i * slot_stride_);
        total_slots_ += n;
        fresh_bytes_ += bytes;
    }

    mutable std::mutex mutex_;
    std::unordered_map<AllocationId, void*> live_;
    std::vector<void*> free_list_;
    std::vector<void*> blocks_;
    AllocationId next_id_ = 0;
    std::size_t object_size_ = 0, align_ = 0, slot_stride_ = 0, capacity_ = 0;
    std::size_t block_slots_ = 8;
    std::size_t total_slots_ = 0;
    std::size_t block_bytes_ = 0;
    std::size_t growth_events_ = 0;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0, granted_bytes_ = 0;
    std::uint64_t pool_hits_ = 0, fresh_allocs_ = 0, reuse_count_ = 0, same_size_reuse_ = 0;
    std::uint64_t reuse_bytes_ = 0, fresh_bytes_ = 0;
    std::uint64_t success_ = 0, failures_ = 0, trim_count_ = 0;
    bool poison_on_free_ = false;
    Bounds bounds_;
};

} // namespace allocator_lab
