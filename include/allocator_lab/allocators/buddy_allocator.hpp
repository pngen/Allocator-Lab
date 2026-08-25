#pragma once

// Allocator Lab 1.0.0
// Buddy allocator: power-of-two split/coalesce over an aligned arena.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <mutex>
#include <set>
#include <unordered_map>
#include <vector>
#include <utility>

#include "allocator_lab/detail/allocator_base.hpp"
#include "allocator_lab/detail/platform.hpp"
#include "allocator_lab/config.hpp"

namespace allocator_lab {

class BuddyAllocator : public detail::AllocatorBase {
public:
    BuddyAllocator(std::size_t arena_log2 = 24, std::string name = "buddy", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::Buddy), max_order_(arena_log2) {
        arena_size_ = std::size_t{1} << max_order_;
        free_.resize(max_order_ + 1);
        arena_ = static_cast<char*>(detail::aligned_alloc(arena_size_, arena_size_));
        if (arena_) {
            BHeader* h = reinterpret_cast<BHeader*>(arena_);
            h->magic = kMagicFree; h->order = static_cast<std::uint32_t>(max_order_); h->in_use = false;
            free_[max_order_].insert(arena_);
        }
        AllocatorCapabilities c;
        c.supports_allocate = true; c.supports_free = true; c.supports_query = true;
        c.supports_trim = false; c.supports_reset = true;
        c.supports_alignment = true; c.is_thread_safe = true;
        c.max_alignment = 16;
        c.max_allocation_size = arena_size_ - header_size;
        c.domains = { MemoryDomain::Host };
        set_capabilities(std::move(c));
    }

    ~BuddyAllocator() { detail::aligned_free(arena_); }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        if (!arena_) { r.error = make_error(ErrorCode::init_failed, "buddy arena unavailable"); return r; }
        if (req.size > capabilities().max_allocation_size) { r.error = make_error(ErrorCode::size_too_large, "too large"); return r; }
        std::size_t align = req.alignment ? req.alignment : 16;
        if (!is_power_of_two(align) || align > 16) { r.error = make_error(ErrorCode::alignment_unsupported, "buddy supports up to 16-byte alignment"); return r; }
        std::size_t payload = align_up(req.size == 0 ? 1 : req.size, 16);
        std::size_t need = header_size + payload;
        std::uint64_t order = order_for(need);
        char* block = nullptr;
        std::uint64_t found_order = order;
        for (std::uint64_t o = order; o <= max_order_; ++o) {
            if (!free_[o].empty()) { block = *free_[o].begin(); found_order = o; break; }
        }
        if (!block) { r.error = make_error(ErrorCode::capacity_exceeded, "buddy capacity reached"); ++failures_; return r; }
        while (found_order > order) {
            --found_order;
            free_[found_order + 1].erase(block);
            std::size_t half = std::size_t{1} << found_order;
            BHeader* h = reinterpret_cast<BHeader*>(block);
            h->magic = kMagicFree; h->order = static_cast<std::uint32_t>(found_order); h->in_use = false;
            free_[found_order].insert(block);
            char* buddy = block + half;
            BHeader* bh = reinterpret_cast<BHeader*>(buddy);
            bh->magic = kMagicFree; bh->order = static_cast<std::uint32_t>(found_order); bh->in_use = false;
            free_[found_order].insert(buddy);
            ++split_count_;
        }
        free_[found_order].erase(block);
        const std::size_t granted = (std::size_t{1} << found_order) - header_size;
        BHeader* h = reinterpret_cast<BHeader*>(block);
        h->magic = kMagicBusy; h->order = static_cast<std::uint32_t>(found_order); h->in_use = true;
        void* p = static_cast<void*>(block + header_size);
        ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
        live_bytes_ += granted; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        requested_bytes_ += req.size; granted_bytes_ += granted;
        if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(p, req.size);
        if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(p, req.size, detail::kPoisonAllocByte);
        AllocationId id = ++next_id_;
        live_.emplace(id, Entry{ block, granted });
        r.error = Error{}; r.id = id; r.address = p; r.size = granted; r.alignment = 16; r.reused_backing = (freed_count_ > 0);
        return r;
    }

    Error free(AllocationHandle handle) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "free of unknown/foreign handle");
        char* block = it->second.addr;
        BHeader* h = reinterpret_cast<BHeader*>(block);
        if (h->magic != kMagicBusy) return make_error(ErrorCode::double_free, "corrupt/double free");
        if (poison_on_free_) detail::fill_poison(block + header_size, it->second.size, detail::kPoisonFreeByte);
        std::uint64_t order = h->order;
        std::uint64_t size = it->second.size;
        h->magic = kMagicFree; h->in_use = false;
        live_.erase(it);
        ++freed_count_;
        coalesce(block, order);
        if (live_count_) --live_count_; if (live_bytes_ >= size) live_bytes_ -= size;
        return Error{};
    }

    Error reset() override {
        std::lock_guard lock(mutex_);
        for (auto& f : free_) f.clear();
        live_.clear(); live_count_ = 0; live_bytes_ = 0;
        if (arena_) {
            BHeader* h = reinterpret_cast<BHeader*>(arena_);
            h->magic = kMagicFree; h->order = static_cast<std::uint32_t>(max_order_); h->in_use = false;
            free_[max_order_].insert(arena_);
        }
        return Error{};
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        char* block = it->second.addr;
        BHeader* h = reinterpret_cast<BHeader*>(block);
        std::size_t payload = (std::size_t{1} << h->order) - header_size;
        out.id = handle; out.size = it->second.size; out.granted_size = payload; out.alignment = 16;
        out.address = static_cast<void*>(block + header_size); out.live = true;
        return Error{};
    }

    AllocatorStatistics statistics() const override {
        std::lock_guard lock(mutex_);
        AllocatorStatistics s;
        s.allocator_id = id_; s.name = name_;
        s.live.live_count = live_count_; s.live.live_bytes = live_bytes_;
        s.live.peak_live_count = peak_live_; s.live.peak_live_bytes = peak_bytes_;
        s.live.reserved = arena_size_; s.live.committed = arena_size_;
        s.accounting.total_requested_bytes = requested_bytes_;
        s.accounting.total_granted_bytes = granted_bytes_;
        s.accounting.internal_waste = granted_bytes_ - requested_bytes_;
        s.fragmentation.internal_observable = true; s.fragmentation.external_observable = true;
        s.fragmentation.internal_fragmentation = arena_size_ > 0 ? 1.0 - (double)live_bytes_ / (double)arena_size_ : 0.0;
        collect_fragmentation(s.fragmentation);
        s.reuse.reuse_count = freed_count_;
        s.reuse.backing_retained_after_workload = arena_size_;
        s.success_count = success_; s.failure_count = failures_;
        s.split_count = split_count_; s.merge_count = merge_count_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        for (auto& f : free_) f.clear();
        live_.clear(); live_count_ = 0; live_bytes_ = 0;
    }

private:
    static constexpr std::size_t header_size = 16;
    static constexpr std::uint64_t kMagicFree = 0xB0DDF0E0ULL;
    static constexpr std::uint64_t kMagicBusy = 0xB0D0B0D0ULL;
    struct BHeader { std::uint64_t magic; std::uint32_t order; bool in_use; };
    struct Entry { char* addr; std::size_t size; };

    static std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) & ~(a - 1); }
    static std::uint64_t order_for(std::size_t need) {
        std::uint64_t o = 0; std::size_t sz = 1;
        while (sz < need) { sz <<= 1; ++o; }
        return o;
    }

    void coalesce(char* block, std::uint64_t order) {
        std::uint64_t o = order;
        char* b = block;
        while (o < max_order_) {
            std::size_t sz = std::size_t{1} << o;
            std::uint64_t off = static_cast<std::uint64_t>(b - arena_);
            std::uint64_t boff = off ^ sz;
            if (boff >= arena_size_) break;
            char* buddy = arena_ + boff;
            auto& fl = free_[o];
            if (!fl.count(buddy)) break;
            fl.erase(buddy);
            if (buddy < b) b = buddy;
            ++o; ++merge_count_;
        }
        BHeader* h = reinterpret_cast<BHeader*>(b);
        h->magic = kMagicFree; h->order = static_cast<std::uint32_t>(o); h->in_use = false;
        free_[o].insert(b);
    }

    void collect_fragmentation(FragmentationMetrics& m) const {
        std::uint64_t largest = 0; std::uint64_t free_bytes = 0; std::uint64_t count = 0;
        for (std::uint64_t o = 0; o <= max_order_; ++o) {
            std::size_t sz = std::size_t{1} << o;
            std::uint64_t n = static_cast<std::uint64_t>(free_[o].size());
            free_bytes += sz * n; count += n;
            if (n > 0 && sz > largest) largest = sz;
        }
        m.free_bytes = free_bytes; m.largest_free_span = largest; m.free_span_count = count;
        m.external_observable = true;
        if (free_bytes > 0) m.external_fragmentation = 1.0 - (double)largest / (double)free_bytes;
    }

    mutable std::mutex mutex_;
    std::vector<std::set<char*>> free_;
    std::unordered_map<AllocationId, Entry> live_;
    AllocationId next_id_ = 0;
    char* arena_ = nullptr;
    std::size_t arena_size_ = 0;
    std::uint64_t max_order_ = 0;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0, granted_bytes_ = 0;
    std::uint64_t success_ = 0, failures_ = 0;
    std::uint64_t split_count_ = 0, merge_count_ = 0, freed_count_ = 0;
    bool poison_on_free_ = false;
    Bounds bounds_;
};

} // namespace allocator_lab
