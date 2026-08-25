#pragma once

// Allocator Lab 1.0.0
// Size-class pool: several bounded size classes with per-class tracking.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include <utility>

#include "allocator_lab/detail/allocator_base.hpp"
#include "allocator_lab/detail/platform.hpp"
#include "allocator_lab/config.hpp"

namespace allocator_lab {

class SizeClassPool : public detail::AllocatorBase {
public:
    explicit SizeClassPool(std::vector<std::size_t> classes = default_classes(), std::size_t max_bytes = size_t(-1),
                           std::string name = "size_class_pool", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::SizeClassPool), max_bytes_(max_bytes) {
        for (std::size_t c : classes) add_class(c);
        AllocatorCapabilities cap;
        cap.supports_allocate = true; cap.supports_free = true; cap.supports_query = true;
        cap.supports_trim = true; cap.supports_reset = true;
        cap.supports_alignment = true; cap.is_thread_safe = true;
        cap.max_alignment = 256; cap.max_allocation_size = bounds_.max_allocation_size;
        cap.domains = { MemoryDomain::Host };
        set_capabilities(std::move(cap));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        if (req.size > bounds_.max_allocation_size) { r.error = make_error(ErrorCode::size_too_large, "too large"); return r; }
        std::size_t align = req.alignment ? req.alignment : 16;
        if (!is_power_of_two(align) || align > 256) { r.error = make_error(ErrorCode::alignment_unsupported, "size-class supports up to 256-byte alignment"); return r; }
        auto idx = class_index_for(req.size);
        if (!idx) { r.error = make_error(ErrorCode::size_too_large, "no size class covers request"); ++failures_; return r; }
        Class& cls = classes_[*idx];
        bool grow = cls.free_list.empty();
        if (grow) {
            if (total_bytes_ + cls.slot_stride * cls.block_slots > max_bytes_) { r.error = make_error(ErrorCode::capacity_exceeded, "size-class capacity reached"); ++failures_; return r; }
            grow_class(cls);
            ++growth_events_;
        }
        if (cls.free_list.empty()) { r.error = make_error(ErrorCode::out_of_memory, "class growth failed"); ++failures_; return r; }
        void* slot = cls.free_list.back(); cls.free_list.pop_back();
        bool reused = !grow;
        if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(slot, cls.size);
        if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(slot, cls.size, detail::kPoisonAllocByte);
        AllocationId id = ++next_id_;
        live_.emplace(id, Entry{ slot, *idx });
        r.error = Error{}; r.id = id; r.address = slot; r.size = cls.size; r.alignment = 16; r.reused_backing = reused;
        ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
        live_bytes_ += cls.size; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        requested_bytes_ += req.size; granted_bytes_ += cls.size;
        cls.alloc_count++; cls.request_bytes += req.size; cls.granted_bytes += cls.size;
        if (reused) { ++pool_hits_; ++reuse_count_; ++same_size_reuse_; } else { ++fresh_allocs_; }
        return r;
    }

    Error free(AllocationHandle handle) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "free of unknown/foreign handle");
        Entry& e = it->second;
        Class& cls = classes_[e.class_idx];
        if (poison_on_free_) detail::fill_poison(e.ptr, cls.size, detail::kPoisonFreeByte);
        cls.free_list.push_back(e.ptr);
        live_.erase(it);
        if (live_count_) --live_count_; if (live_bytes_ >= cls.size) live_bytes_ -= cls.size;
        ++same_size_reuse_;
        return Error{};
    }

    Error trim() override {
        std::lock_guard lock(mutex_);
        ++trim_count_;
        return Error{};
    }

    Error reset() override {
        std::lock_guard lock(mutex_);
        for (auto& cls : classes_) {
            for (auto* b : cls.blocks) detail::aligned_free(b);
            cls.blocks.clear(); cls.free_list.clear(); cls.total_slots = 0;
        }
        live_.clear(); live_count_ = 0; live_bytes_ = 0; total_bytes_ = 0;
        return Error{};
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        Class& cls = classes_[it->second.class_idx];
        out.id = handle; out.size = cls.size; out.granted_size = cls.size; out.alignment = 16;
        out.address = it->second.ptr; out.live = true;
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
        s.reuse.backing_retained_after_workload = total_bytes_;
        s.fragmentation.internal_observable = true; s.fragmentation.external_observable = false;
        s.fragmentation.internal_fragmentation = granted_bytes_ > 0 ? 1.0 - (double)requested_bytes_ / (double)granted_bytes_ : 0.0;
        s.growth_events = growth_events_;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        for (auto& cls : classes_) {
            for (auto* b : cls.blocks) detail::aligned_free(b);
            cls.blocks.clear(); cls.free_list.clear();
        }
        live_.clear(); live_count_ = 0; live_bytes_ = 0; total_bytes_ = 0;
    }

    const std::vector<std::size_t>& class_sizes() const { return class_sizes_; }
    std::uint64_t class_alloc_count(std::size_t i) const { return i < classes_.size() ? classes_[i].alloc_count : 0; }

private:
    struct Entry { void* ptr; std::size_t class_idx; };
    struct Class {
        std::size_t size = 0;
        std::size_t slot_stride = 0;
        std::size_t block_slots = 8;
        std::vector<void*> free_list;
        std::vector<void*> blocks;
        std::size_t total_slots = 0;
        std::uint64_t alloc_count = 0;
        std::uint64_t request_bytes = 0;
        std::uint64_t granted_bytes = 0;
    };

    static std::vector<std::size_t> default_classes() {
        std::vector<std::size_t> c;
        for (std::size_t s = 16; s <= 65536; s *= 2) c.push_back(s);
        c.push_back(128 * 1024); c.push_back(256 * 1024); c.push_back(512 * 1024);
        c.push_back(1024 * 1024); c.push_back(4 * 1024 * 1024);
        return c;
    }

    void add_class(std::size_t size) {
        Class c; c.size = size; c.slot_stride = round_up_pow2_fn(size > 16 ? size : 16);
        classes_.push_back(c);
        class_sizes_.push_back(size);
    }

    std::optional<std::size_t> class_index_for(std::size_t req) const {
        for (std::size_t i = 0; i < classes_.size(); ++i) if (classes_[i].size >= req) return i;
        return std::nullopt;
    }

    static std::size_t round_up_pow2_fn(std::size_t v) {
        if (v <= 1) return 1;
        --v; v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v |= v >> 32; return v + 1;
    }

    void grow_class(Class& cls) {
        std::size_t n = cls.block_slots;
        std::size_t bytes = cls.slot_stride * n;
        void* base = detail::aligned_alloc(bytes, 16);
        if (!base) return;
        cls.blocks.push_back(base);
        char* p = static_cast<char*>(base);
        for (std::size_t i = 0; i < n; ++i) cls.free_list.push_back(p + i * cls.slot_stride);
        cls.total_slots += n;
        total_bytes_ += bytes;
        if (total_bytes_ > peak_total_) peak_total_ = total_bytes_;
    }

    mutable std::mutex mutex_;
    std::vector<Class> classes_;
    std::vector<std::size_t> class_sizes_;
    std::unordered_map<AllocationId, Entry> live_;
    AllocationId next_id_ = 0;
    std::size_t max_bytes_ = 0;
    std::uint64_t total_bytes_ = 0;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0, granted_bytes_ = 0;
    std::uint64_t pool_hits_ = 0, fresh_allocs_ = 0, reuse_count_ = 0, same_size_reuse_ = 0;
    std::uint64_t success_ = 0, failures_ = 0, growth_events_ = 0, trim_count_ = 0;
    std::uint64_t peak_total_ = 0;
    bool poison_on_free_ = false;
    Bounds bounds_;
};

} // namespace allocator_lab
