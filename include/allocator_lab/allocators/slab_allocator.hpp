#pragma once

// Allocator Lab 1.0.0
// Slab allocator: page-backed fixed-category allocation with slab recycling.
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

class SlabAllocator : public detail::AllocatorBase {
public:
    SlabAllocator(std::size_t object_size, std::size_t page_size = 4096, std::size_t max_bytes = size_t(-1),
                  std::string name = "slab", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::Slab),
          object_size_(object_size), page_size_(page_size), max_bytes_(max_bytes) {
        slot_stride_ = align_up(object_size_ > 16 ? object_size_ : 16, 16);
        total_slots_per_slab_ = (page_size_ - header_size) / slot_stride_;
        if (total_slots_per_slab_ == 0) total_slots_per_slab_ = 1;
        AllocatorCapabilities c;
        c.supports_allocate = true; c.supports_free = true; c.supports_query = true;
        c.supports_trim = true; c.supports_reset = true;
        c.supports_alignment = true; c.is_thread_safe = true;
        c.max_alignment = 16; c.max_allocation_size = object_size_;
        c.domains = { MemoryDomain::Host };
        set_capabilities(std::move(c));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        if (req.size > object_size_) { r.error = make_error(ErrorCode::size_too_large, "object exceeds slab object_size"); return r; }
        std::size_t align = req.alignment ? req.alignment : 16;
        if (!is_power_of_two(align) || align > 16) { r.error = make_error(ErrorCode::alignment_unsupported, "slab supports 16-byte alignment"); return r; }
        Slab* slab = find_slab_with_free();
        if (!slab) {
            if (!grow_slab()) { r.error = make_error(ErrorCode::capacity_exceeded, "slab capacity reached"); ++failures_; return r; }
            slab = &slabs_.back();
            ++growth_events_;
        }
        void* slot = slab->free_list.back(); slab->free_list.pop_back();
        bool reused = slab->free_list.size() < slab->total_slots;
        if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(slot, object_size_);
        if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(slot, object_size_, detail::kPoisonAllocByte);
        AllocationId id = ++next_id_;
        live_.emplace(id, slot);
        r.error = Error{}; r.id = id; r.address = slot; r.size = object_size_; r.alignment = 16; r.reused_backing = reused;
        ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
        live_bytes_ += object_size_; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        requested_bytes_ += req.size; granted_bytes_ += object_size_;
        if (reused) { ++pool_hits_; ++reuse_count_; ++same_size_reuse_; } else { ++fresh_allocs_; }
        return r;
    }

    Error free(AllocationHandle handle) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "free of unknown/foreign handle");
        void* slot = it->second;
        Slab* slab = slab_of(slot);
        if (!slab) return make_error(ErrorCode::invalid_handle, "free of foreign handle");
        if (poison_on_free_) detail::fill_poison(slot, object_size_, detail::kPoisonFreeByte);
        slab->free_list.push_back(slot);
        live_.erase(it);
        if (live_count_) --live_count_; if (live_bytes_ >= object_size_) live_bytes_ -= object_size_;
        ++same_size_reuse_;
        return Error{};
    }

    Error trim() override {
        std::lock_guard lock(mutex_);
        release_empty_slabs();
        ++trim_count_;
        return Error{};
    }

    Error reset() override {
        std::lock_guard lock(mutex_);
        for (auto& s : slabs_) detail::aligned_free(s.base);
        slabs_.clear(); live_.clear();
        live_count_ = 0; live_bytes_ = 0; total_bytes_ = 0;
        return Error{};
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        out.id = handle; out.size = object_size_; out.granted_size = object_size_;
        out.alignment = 16; out.address = it->second; out.live = true;
        return Error{};
    }

    AllocatorStatistics statistics() const override {
        std::lock_guard lock(mutex_);
        AllocatorStatistics s;
        s.allocator_id = id_; s.name = name_;
        s.live.live_count = live_count_; s.live.live_bytes = live_bytes_;
        s.live.peak_live_count = peak_live_; s.live.peak_live_bytes = peak_bytes_;
        s.live.reserved = total_bytes_; s.live.committed = total_bytes_;
        s.accounting.total_requested_bytes = requested_bytes_;
        s.accounting.total_granted_bytes = granted_bytes_;
        s.accounting.internal_waste = granted_bytes_ - requested_bytes_;
        s.reuse.pool_hits = pool_hits_; s.reuse.fresh_allocations = fresh_allocs_;
        s.reuse.reuse_count = reuse_count_; s.reuse.same_size_reuse = same_size_reuse_;
        s.reuse.trim_recovery_bytes = trim_recovered_bytes_;
        s.reuse.backing_retained_after_workload = total_bytes_;
        s.fragmentation.internal_observable = true; s.fragmentation.external_observable = false;
        s.fragmentation.internal_fragmentation = granted_bytes_ > 0 ? 1.0 - (double)requested_bytes_ / (double)granted_bytes_ : 0.0;
        s.growth_events = growth_events_;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        for (auto& s : slabs_) detail::aligned_free(s.base);
        slabs_.clear(); live_.clear();
        live_count_ = 0; live_bytes_ = 0; total_bytes_ = 0;
    }

private:
    static constexpr std::size_t header_size = 64;
    static constexpr std::uint64_t kMagic = 0x51A8B5A8ULL;
    struct Slab {
        char* base = nullptr;
        std::size_t total_slots = 0;
        std::vector<void*> free_list;
        std::uint64_t magic = kMagic;
    };

    static std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) & ~(a - 1); }

    Slab* slab_of(const void* slot) {
        char* p = static_cast<char*>(const_cast<void*>(slot));
        char* base = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(p) & ~(static_cast<std::uintptr_t>(page_size_) - 1));
        for (auto& s : slabs_) if (s.base == base) return &s;
        return nullptr;
    }

    Slab* find_slab_with_free() {
        for (auto& s : slabs_) if (!s.free_list.empty()) return &s;
        return nullptr;
    }

    bool grow_slab() {
        if (total_bytes_ + page_size_ > max_bytes_) return false;
        void* base = detail::aligned_alloc(page_size_, page_size_);
        if (!base) return false;
        Slab s;
        s.base = static_cast<char*>(base);
        s.total_slots = total_slots_per_slab_;
        char* p = s.base + header_size;
        for (std::size_t i = 0; i < s.total_slots; ++i) s.free_list.push_back(p + i * slot_stride_);
        slabs_.push_back(s);
        total_bytes_ += page_size_;
        if (total_bytes_ > peak_total_) peak_total_ = total_bytes_;
        return true;
    }

    void release_empty_slabs() {
        for (auto it = slabs_.begin(); it != slabs_.end();) {
            if (it->free_list.size() == it->total_slots) {
                detail::aligned_free(it->base);
                trim_recovered_bytes_ += page_size_;
                total_bytes_ -= page_size_;
                it = slabs_.erase(it);
            } else ++it;
        }
    }

    mutable std::mutex mutex_;
    std::vector<Slab> slabs_;
    std::unordered_map<AllocationId, void*> live_;
    AllocationId next_id_ = 0;
    std::size_t object_size_ = 0, page_size_ = 0, max_bytes_ = 0;
    std::size_t slot_stride_ = 0, total_slots_per_slab_ = 0;
    std::uint64_t total_bytes_ = 0;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0, granted_bytes_ = 0;
    std::uint64_t pool_hits_ = 0, fresh_allocs_ = 0, reuse_count_ = 0, same_size_reuse_ = 0;
    std::uint64_t success_ = 0, failures_ = 0, growth_events_ = 0, trim_count_ = 0;
    std::uint64_t trim_recovered_bytes_ = 0, peak_total_ = 0;
    bool poison_on_free_ = false;
    Bounds bounds_;
};

} // namespace allocator_lab
