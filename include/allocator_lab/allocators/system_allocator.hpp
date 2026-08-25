#pragma once

// Allocator Lab 1.0.0
// Direct system allocator: std::malloc / std::free (or realloc).
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>

#include "allocator_lab/detail/allocator_base.hpp"
#include "allocator_lab/detail/platform.hpp"
#include "allocator_lab/metrics.hpp"
#include "allocator_lab/config.hpp"

namespace allocator_lab {

class SystemAllocator : public detail::AllocatorBase {
public:
    explicit SystemAllocator(std::string name = "system", std::uint64_t field_id = 0)
        : AllocatorBase(field_id, std::move(name), AllocatorKind::System) {
        AllocatorCapabilities c;
        c.supports_allocate = true;
        c.supports_free = true;
        c.supports_reallocate = true;
        c.supports_query = true;
        c.supports_trim = false;
        c.supports_reset = false;
        c.supports_alignment = false;
        c.is_thread_safe = true;
        c.max_alignment = detail::default_alignment;
        c.max_allocation_size = bounds_.max_allocation_size;
        c.domains = { MemoryDomain::Host };
        set_capabilities(std::move(c));
    }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        if (req.size > bounds_.max_allocation_size) {
            r.error = make_error(ErrorCode::size_too_large, "request exceeds max allocation size");
            return r;
        }
        std::size_t align = req.alignment ? req.alignment : detail::default_alignment;
        if (!is_power_of_two(align)) {
            r.error = make_error(ErrorCode::invalid_alignment, "alignment must be power of two");
            return r;
        }
        if (align > detail::default_alignment) {
            r.error = make_error(ErrorCode::alignment_unsupported, "system allocator cannot honor alignment\n");
            return r;
        }
        void* p = detail::system_alloc(req.size == 0 ? 1 : req.size);
        if (!p) { r.error = make_error(ErrorCode::out_of_memory, "system allocation failed"); return r; }
        if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(p, req.size);
        if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(p, req.size, detail::kPoisonAllocByte);
        AllocationId id = ++next_id_;
        records_.emplace(id, Record{ p, req.size, align, req.domain, req.tag, req.flags });
        r.error = Error{};
        r.id = id;
        r.address = p;
        r.size = req.size;
        r.alignment = align;
        r.reused_backing = false;
        update_accounting_allocate(req.size);
        return r;
    }

    Error free(AllocationHandle handle) override {
        std::lock_guard lock(mutex_);
        auto it = records_.find(handle);
        if (it == records_.end()) return make_error(ErrorCode::invalid_handle, "free of unknown/foreign handle");
        if (has(it->second.flags, AllocationFlags::PoisonFree) || poison_on_free_)
            detail::fill_poison(it->second.ptr, it->second.size, detail::kPoisonFreeByte);
        detail::system_free(it->second.ptr);
        update_accounting_free(it->second.size);
        records_.erase(it);
        return Error{};
    }

    AllocationResult reallocate(AllocationHandle handle, const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
        auto it = records_.find(handle);
        if (it == records_.end()) { r.error = make_error(ErrorCode::invalid_handle, "realloc of unknown handle"); return r; }
        if (req.size > bounds_.max_allocation_size) { r.error = make_error(ErrorCode::size_too_large, "realloc exceeds max"); return r; }
        void* np = std::realloc(it->second.ptr, req.size == 0 ? 1 : req.size);
        if (!np) { r.error = make_error(ErrorCode::out_of_memory, "realloc failed; original preserved"); return r; }
        // Update accounting: remove old granted, add new.
        update_accounting_realloc(it->second.size, req.size);
        it->second.ptr = np;
        it->second.size = req.size;
        it->second.tag = req.tag;
        r.error = Error{};
        r.id = handle;
        r.address = np;
        r.size = req.size;
        r.alignment = it->second.alignment;
        r.reused_backing = false;
        return r;
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = records_.find(handle);
        if (it == records_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        out.id = handle; out.size = it->second.size; out.granted_size = it->second.size;
        out.alignment = it->second.alignment; out.domain = it->second.domain;
        out.address = it->second.ptr; out.live = true; out.tag = it->second.tag;
        return Error{};
    }

    AllocatorStatistics statistics() const override {
        std::lock_guard lock(mutex_);
        AllocatorStatistics s;
        s.allocator_id = id_; s.name = name_;
        s.live.live_count = static_cast<std::uint64_t>(records_.size());
        s.live.live_bytes = live_bytes_;
        s.live.peak_live_count = peak_live_;
        s.live.peak_live_bytes = peak_bytes_;
        s.accounting.total_requested_bytes = requested_bytes_;
        s.accounting.total_granted_bytes = granted_bytes_;
        s.accounting.internal_waste = granted_bytes_ - requested_bytes_;
        s.fragmentation.internal_observable = false;
        s.fragmentation.external_observable = false;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
        for (auto& kv : records_) detail::system_free(kv.second.ptr);
        records_.clear();
        live_bytes_ = 0; granted_bytes_ = 0; requested_bytes_ = 0;
        peak_live_ = 0; peak_bytes_ = 0;
    }

private:
    struct Record { void* ptr; std::size_t size; std::size_t alignment; MemoryDomain domain; std::uint32_t tag; AllocationFlags flags; };
    void update_accounting_allocate(std::size_t sz) {
        requested_bytes_ += sz; granted_bytes_ += sz;
        live_bytes_ += sz;
        ++success_;
        if (records_.size() > peak_live_) peak_live_ = static_cast<std::uint64_t>(records_.size());
        if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
    }
    void update_accounting_free(std::size_t sz) { if (live_bytes_ >= sz) live_bytes_ -= sz; else live_bytes_ = 0; }
    void update_accounting_realloc(std::size_t oldsz, std::size_t newsz) {
        if (live_bytes_ >= oldsz) live_bytes_ -= oldsz; else live_bytes_ = 0;
        live_bytes_ += newsz;
        granted_bytes_ += (newsz > oldsz ? newsz - oldsz : 0);
        requested_bytes_ += newsz;
        if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
    }

    mutable std::mutex mutex_;
    std::unordered_map<AllocationId, Record> records_;
    AllocationId next_id_ = 0;
    std::uint64_t live_bytes_ = 0;
    std::uint64_t requested_bytes_ = 0;
    std::uint64_t granted_bytes_ = 0;
    std::uint64_t peak_live_ = 0;
    std::uint64_t peak_bytes_ = 0;
    std::uint64_t success_ = 0;
    std::uint64_t failures_ = 0;
    bool poison_on_free_ = false;
    Bounds bounds_;
};

} // namespace allocator_lab
