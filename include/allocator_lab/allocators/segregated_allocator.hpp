#pragma once

// Allocator Lab 1.0.0
// Segregated-fit experimental allocator: bounded segregated free lists by
// size class, with splitting and NO coalescing (so fragmentation is visible).
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

class SegregatedAllocator : public detail::AllocatorBase {
public:
    SegregatedAllocator(std::size_t superblock_size = 128u * 1024, std::size_t max_bytes = size_t(-1),
                        std::string name = "segregated_fit", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::SegregatedFit),
          superblock_size_(superblock_size), max_bytes_(max_bytes) {
        max_bin_ = ceil_log2(superblock_size_);
        bins_.resize(max_bin_ + 1);
        AllocatorCapabilities c;
        c.supports_allocate = true; c.supports_free = true; c.supports_query = true;
        c.supports_trim = false; c.supports_reset = true;
        c.supports_alignment = true; c.is_thread_safe = true; c.is_experimental = true;
        c.max_alignment = detail::default_alignment;
        c.max_allocation_size = bounds_.max_allocation_size;
        c.domains = { MemoryDomain::Host };
        c.notes = "experimental segregated-fit; no coalescing (fragmentation explicit)";
        set_capabilities(std::move(c));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        if (req.size > bounds_.max_allocation_size) { r.error = make_error(ErrorCode::size_too_large, "too large"); return r; }
        std::size_t align = req.alignment ? req.alignment : detail::default_alignment;
        if (!is_power_of_two(align) || align > detail::default_alignment) { r.error = make_error(ErrorCode::alignment_unsupported, "segregated supports natural alignment"); return r; }
        std::size_t payload = align_up(req.size == 0 ? 1 : req.size, detail::default_alignment);
        std::size_t total_need = header_size + payload;
        Block* b = find_block(total_need);
        if (!b) {
            if (!grow_superblock(total_need)) { r.error = make_error(ErrorCode::capacity_exceeded, "segregated capacity reached"); ++failures_; return r; }
            b = find_block(total_need);
            if (!b) { r.error = make_error(ErrorCode::out_of_memory, "segregated allocation failed"); ++failures_; return r; }
        }
        void* p = static_cast<char*>(static_cast<void*>(b)) + header_size;
        const std::size_t granted = payload_of(b);
        if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(p, req.size);
        if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(p, req.size, detail::kPoisonAllocByte);
        AllocationId id = ++next_id_;
        live_.emplace(id, b);
        r.error = Error{}; r.id = id; r.address = p; r.size = granted; r.alignment = align; r.reused_backing = (freed_count_ > 0);
        ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
        live_bytes_ += granted; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
        requested_bytes_ += req.size; granted_bytes_ += granted;
        return r;
    }

    Error free(AllocationHandle handle) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "free of unknown/foreign handle");
        Block* b = it->second;
        if (b->magic != kMagicBusy) return make_error(ErrorCode::double_free, "corrupt/double free");
        if (poison_on_free_) detail::fill_poison(static_cast<char*>(static_cast<void*>(b)) + header_size, b->size - header_size, detail::kPoisonFreeByte);
        b->magic = kMagicFree; b->busy = false;
        insert_bin(b);
        live_.erase(it);
        ++freed_count_; ++same_size_reuse_;
        if (live_count_) --live_count_; if (live_bytes_ >= payload_of(b)) live_bytes_ -= payload_of(b);
        return Error{};
    }

    Error reset() override {
        std::lock_guard lock(mutex_);
        for (auto* sb : superblocks_) detail::system_free(sb);
        superblocks_.clear(); live_.clear();
        for (auto& bin : bins_) bin.clear();
        live_count_ = 0; live_bytes_ = 0; total_bytes_ = 0;
        return Error{};
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        Block* b = it->second;
        out.id = handle; out.size = payload_of(b); out.granted_size = payload_of(b);
        out.alignment = detail::default_alignment; out.address = static_cast<char*>(static_cast<void*>(b)) + header_size; out.live = true;
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
        s.fragmentation.internal_observable = true; s.fragmentation.external_observable = true;
        s.fragmentation.internal_fragmentation = total_bytes_ > 0 ? 1.0 - (double)live_bytes_ / (double)total_bytes_ : 0.0;
        collect_fragmentation(s.fragmentation);
        s.reuse.reuse_count = freed_count_;
        s.reuse.backing_retained_after_workload = total_bytes_;
        s.split_count = split_count_;
        s.search_depth_total = search_depth_;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        for (auto* sb : superblocks_) detail::system_free(sb);
        superblocks_.clear(); live_.clear();
        for (auto& bin : bins_) bin.clear();
        live_count_ = 0; live_bytes_ = 0; total_bytes_ = 0;
    }

private:
    static constexpr std::size_t header_size = 16;
    static constexpr std::size_t min_block = header_size + 8;
    static constexpr std::uint64_t kMagicFree = 0x5E6AFAF0ULL;
    static constexpr std::uint64_t kMagicBusy = 0x5E6AFAF1ULL;
    struct Block { std::uint64_t magic; std::size_t size; bool busy; };

    static std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) & ~(a - 1); }
    static std::uint64_t ceil_log2(std::size_t v) { std::uint64_t r = 0; std::size_t s = 1; while (s < v) { s <<= 1; ++r; } return r; }
    static std::size_t payload_of(Block* b) { return b->size - header_size; }
    static void* ptr_of(Block* b) { return static_cast<char*>(static_cast<void*>(b)) + header_size; }

    void insert_bin(Block* b) {
        std::uint64_t bin = ceil_log2(b->size);
        if (bin > max_bin_) bin = max_bin_;
        bins_[bin].push_back(reinterpret_cast<char*>(b));
    }

    Block* find_block(std::size_t need) {
        std::uint64_t start = ceil_log2(need);
        ++search_depth_;
        for (std::uint64_t bin = start; bin <= max_bin_; ++bin) {
            auto& fl = bins_[bin];
            for (auto it = fl.begin(); it != fl.end(); ++it) {
                ++search_depth_;
                Block* b = reinterpret_cast<Block*>(*it);
                if (b->size >= need) {
                    fl.erase(it);
                    if (b->size - need >= min_block) {
                        Block* rest = reinterpret_cast<Block*>(reinterpret_cast<char*>(b) + need);
                        rest->magic = kMagicFree; rest->size = b->size - need; rest->busy = false;
                        insert_bin(rest);
                        b->size = need;
                        ++split_count_;
                    }
                    b->magic = kMagicBusy; b->busy = true;
                    return b;
                }
            }
        }
        return nullptr;
    }

    bool grow_superblock(std::size_t need) {
        std::size_t sz = superblock_size_ > need ? superblock_size_ : align_up(need, superblock_size_);
        if (total_bytes_ + sz > max_bytes_) return false;
        void* mem = detail::system_alloc(sz);
        if (!mem) return false;
        superblocks_.push_back(mem);
        Block* b = reinterpret_cast<Block*>(mem);
        b->magic = kMagicFree; b->size = sz; b->busy = false;
        insert_bin(b);
        total_bytes_ += sz;
        if (total_bytes_ > peak_total_) peak_total_ = total_bytes_;
        ++growth_events_;
        return true;
    }

    void collect_fragmentation(FragmentationMetrics& m) const {
        std::uint64_t free_bytes = 0, largest = 0, count = 0;
        for (const auto& bin : bins_) {
            for (const auto* p : bin) {
                const Block* b = reinterpret_cast<const Block*>(p);
                free_bytes += b->size; ++count; if (b->size > largest) largest = b->size;
            }
        }
        m.free_bytes = free_bytes; m.largest_free_span = largest; m.free_span_count = count;
        m.external_observable = true;
        if (free_bytes > 0) m.external_fragmentation = 1.0 - (double)largest / (double)free_bytes;
    }

    mutable std::mutex mutex_;
    std::vector<std::vector<char*>> bins_;
    std::vector<void*> superblocks_;
    std::unordered_map<AllocationId, Block*> live_;
    AllocationId next_id_ = 0;
    std::size_t superblock_size_ = 0, max_bytes_ = 0, total_bytes_ = 0;
    std::uint64_t max_bin_ = 0;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0, granted_bytes_ = 0;
    std::uint64_t success_ = 0, failures_ = 0;
    std::uint64_t split_count_ = 0, freed_count_ = 0, same_size_reuse_ = 0;
    std::uint64_t search_depth_ = 0, growth_events_ = 0, peak_total_ = 0;
    bool poison_on_free_ = false;
    Bounds bounds_;
};

} // namespace allocator_lab
