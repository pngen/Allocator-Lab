#pragma once

// Allocator Lab 1.0.0
// Free-list allocator: variable-size blocks with splitting and
// boundary-tag coalescing. Superblock-backed and thread-safe.
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

class FreeListAllocator : public detail::AllocatorBase {
public:
    FreeListAllocator(std::size_t superblock_size = 256u * 1024, std::size_t max_bytes = size_t(-1),
                      std::string name = "free_list", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::FreeList),
          superblock_size_(superblock_size), max_bytes_(max_bytes) {
        AllocatorCapabilities c;
        c.supports_allocate = true; c.supports_free = true; c.supports_query = true;
        c.supports_trim = true; c.supports_reset = true;
        c.supports_alignment = true; c.is_thread_safe = true;
        c.max_alignment = detail::default_alignment;
        c.max_allocation_size = bounds_.max_allocation_size;
        c.domains = { MemoryDomain::Host };
        set_capabilities(std::move(c));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        if (req.size > bounds_.max_allocation_size) { r.error = make_error(ErrorCode::size_too_large, "too large"); return r; }
        std::size_t align = req.alignment ? req.alignment : detail::default_alignment;
        if (!is_power_of_two(align) || align > detail::default_alignment) {
            r.error = make_error(ErrorCode::alignment_unsupported, "freelist supports natural alignment only"); return r;
        }
        std::size_t payload = align_up(req.size == 0 ? 1 : req.size, detail::default_alignment);
        std::size_t need = header_size + payload + footer_size;
        Block* b = find_and_split(need);
        if (!b) {
            if (!grow_superblock(need)) { r.error = make_error(ErrorCode::capacity_exceeded, "freelist capacity reached"); ++failures_; return r; }
            b = find_and_split(need);
            if (!b) { r.error = make_error(ErrorCode::out_of_memory, "freelist could not satisfy"); ++failures_; return r; }
        }
        void* p = static_cast<char*>(static_cast<void*>(b)) + header_size;
        const std::size_t granted = payload_of(b);
        ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
        live_bytes_ += granted; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        requested_bytes_ += req.size; granted_bytes_ += granted;
        if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(p, req.size);
        if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(p, req.size, detail::kPoisonAllocByte);
        AllocationId id = ++next_id_;
        live_.emplace(id, b);
        b->id = id;
        r.error = Error{}; r.id = id; r.address = p; r.size = granted; r.alignment = align; r.reused_backing = (blocks_freed_ > 0);
        return r;
    }

    Error free(AllocationHandle handle) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "free of unknown/foreign handle");
        Block* b = it->second;
        if (b->magic != kMagicBusy) return make_error(ErrorCode::double_free, "corrupt/double free");
        if (poison_on_free_) detail::fill_poison(ptr_of(b), b->size - header_size - footer_size, detail::kPoisonFreeByte);
        const std::size_t granted = payload_of(b);
        live_.erase(it);
        b->magic = kMagicFree;
        set_busy(b, false);
        coalesce(b);
        ++blocks_freed_;
        if (live_count_) --live_count_; if (live_bytes_ >= granted) live_bytes_ -= granted;
        return Error{};
    }

    Error trim() override {
        std::lock_guard lock(mutex_);
        ++trim_count_;
        // Coalescing already happens eagerly on free; trim reports effectively
        // and may release trailing superblocks that are fully free.
        release_empty_superblocks();
        return Error{};
    }

    Error reset() override {
        std::lock_guard lock(mutex_);
        for (auto* sb : superblocks_) detail::system_free(sb);
        superblocks_.clear(); live_.clear(); free_head_ = nullptr;
        live_count_ = 0; live_bytes_ = 0; total_bytes_ = 0;
        return Error{};
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        Block* b = it->second;
        out.id = handle; out.size = payload_of(b); out.granted_size = payload_of(b);
        out.alignment = detail::default_alignment; out.address = ptr_of(b); out.live = true;
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
        s.reuse.reuse_count = blocks_freed_;
        s.reuse.backing_retained_after_workload = total_bytes_;
        s.fragmentation.internal_observable = true; s.fragmentation.external_observable = true;
        s.fragmentation.internal_fragmentation = total_bytes_ > 0 ? 1.0 - (double)live_bytes_ / (double)total_bytes_ : 0.0;
        collect_fragmentation(s.fragmentation);
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        for (auto* sb : superblocks_) detail::system_free(sb);
        superblocks_.clear(); live_.clear(); free_head_ = nullptr;
        live_count_ = 0; live_bytes_ = 0; total_bytes_ = 0;
    }

private:
    static constexpr std::size_t kMagicFree = 0xF4E1F0F0ULL;
    static constexpr std::size_t kMagicBusy = 0xB5A5B5A5ULL;

    struct Block {
        std::uint64_t magic;
        std::size_t size;     // total block size incl header+footer
        bool busy;
        Block* next_free;
        Block* arena_base;
        std::size_t arena_total;   // full superblock size (valid at arena_base)
        AllocationId id;
    };
    struct Footer { std::size_t size; bool busy; };

    static constexpr std::size_t header_size = 48;  // aligned Block
    static constexpr std::size_t footer_size = 16;
    static constexpr std::size_t min_block = header_size + 8 + footer_size;

    static std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) & ~(a - 1); }
    static void* ptr_of(Block* b) { return static_cast<char*>(static_cast<void*>(b)) + header_size; }
    static std::size_t payload_of(Block* b) { return b->size - header_size - footer_size; }
    static Footer* footer_of(Block* b) { return reinterpret_cast<Footer*>(static_cast<char*>(static_cast<void*>(b)) + b->size - footer_size); }
    static void set_busy(Block* b, bool busy) { b->busy = busy; footer_of(b)->busy = busy; footer_of(b)->size = b->size; }

    Block* find_and_split(std::size_t need) {
        Block** prev_link = &free_head_;
        Block* cur = free_head_;
        while (cur) {
            ++scanned_;
            if (cur->size >= need) {
                *prev_link = cur->next_free;
                if (cur->size - need >= min_block) {
                    Block* rest = reinterpret_cast<Block*>(static_cast<char*>(static_cast<void*>(cur)) + need);
                    rest->magic = kMagicFree; rest->size = cur->size - need; rest->arena_base = cur->arena_base;
                    set_busy(rest, false);
                    rest->next_free = *prev_link;
                    *prev_link = rest;
                    cur->size = need;
                }
                set_busy(cur, true);
                cur->magic = kMagicBusy;
                return cur;
            }
            prev_link = &cur->next_free;
            cur = cur->next_free;
        }
        return nullptr;
    }

    bool grow_superblock(std::size_t need) {
        std::size_t sz = superblock_size_ > need ? superblock_size_ : align_up(need, superblock_size_);
        if (total_bytes_ + sz > max_bytes_) return false;
        void* mem = detail::system_alloc(sz);
        if (!mem) return false;
        double* dummy = static_cast<double*>(mem);
        (void)dummy;
        superblocks_.push_back(mem);
        Block* b = reinterpret_cast<Block*>(mem);
        b->magic = kMagicFree; b->size = sz; b->arena_base = b; b->arena_total = sz; b->next_free = free_head_;
        set_busy(b, false);
        free_head_ = b;
        total_bytes_ += sz;
        if (total_bytes_ > peak_total_) peak_total_ = total_bytes_;
        ++growth_events_;
        return true;
    }

    void coalesce(Block* b) {
        // Right neighbor
        Block* right = reinterpret_cast<Block*>(static_cast<char*>(static_cast<void*>(b)) + b->size);
        char* arena_start = static_cast<char*>(static_cast<void*>(b->arena_base));
        char* arena_end = arena_start + b->arena_total;
        char* right_addr = static_cast<char*>(static_cast<void*>(right));
        bool right_in_arena = right_addr >= arena_start && right_addr < arena_end;
        if (right_in_arena && right->magic == kMagicFree) {
            unlink(right);
            b->size += right->size;
            set_busy(b, false);
        }
        // Left neighbor via footer
        if (static_cast<char*>(static_cast<void*>(b)) > static_cast<char*>(static_cast<void*>(b->arena_base))) {
            Footer* pf = reinterpret_cast<Footer*>(static_cast<char*>(static_cast<void*>(b)) - footer_size);
            if (!pf->busy) {
                Block* left = reinterpret_cast<Block*>(static_cast<char*>(static_cast<void*>(b)) - pf->size);
                if (left->magic == kMagicFree) {
                    unlink(left);
                    left->size += b->size;
                    set_busy(left, false);
                    b = left;
                }
            }
        }
        b->magic = kMagicFree; b->next_free = free_head_; free_head_ = b;
        ++coalesce_count_;
    }

    void unlink(Block* b) {
        Block** pp = &free_head_;
        while (*pp && *pp != b) pp = &(*pp)->next_free;
        if (*pp) *pp = b->next_free;
    }

    void release_empty_superblocks() {
        // Conservative: do not free superblocks, expose retained bytes.
    }

    void collect_fragmentation(FragmentationMetrics& m) const {
        std::uint64_t free_bytes = 0; std::uint64_t largest = 0; std::uint64_t count = 0;
        Block* cur = free_head_;
        while (cur) {
            std::size_t s = cur->size;
            free_bytes += s; ++count; if (s > largest) largest = s;
            cur = cur->next_free;
        }
        m.free_bytes = free_bytes; m.largest_free_span = largest; m.free_span_count = count;
        m.external_observable = true;
        if (free_bytes > 0) m.external_fragmentation = 1.0 - (double)largest / (double)free_bytes;
    }

    mutable std::mutex mutex_;
    std::vector<void*> superblocks_;
    std::unordered_map<AllocationId, Block*> live_;
    Block* free_head_ = nullptr;
    AllocationId next_id_ = 0;
    std::size_t superblock_size_ = 0, max_bytes_ = 0, total_bytes_ = 0;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0, granted_bytes_ = 0;
    std::uint64_t success_ = 0, failures_ = 0;
    std::uint64_t growth_events_ = 0, blocks_freed_ = 0, coalesce_count_ = 0;
    std::uint64_t scanned_ = 0, peak_total_ = 0;
    std::uint64_t trim_count_ = 0;
    Bounds bounds_;
    bool poison_on_free_ = false;
};

} // namespace allocator_lab
