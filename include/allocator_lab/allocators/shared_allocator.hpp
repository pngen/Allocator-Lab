#pragma once

// Allocator Lab 1.0.0
// Interprocess shared-memory allocator (real Windows file mapping).
//
// A single named mapping is shared by processes. Allocation is arena-like
// (monotonic bump) inside the shared region using InterlockedCompareExchange,
// so separate OS processes can allocate from the same backing. A published
// block slot lets a second process locate and verify a creator's allocation.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <cstring>

#include "allocator_lab/detail/allocator_base.hpp"
#include "allocator_lab/detail/platform.hpp"
#include "allocator_lab/config.hpp"

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
#endif

namespace allocator_lab {

class SharedAllocator : public detail::AllocatorBase {
public:
    enum class Mode { Create, Open };

    SharedAllocator(std::string name, std::size_t total_bytes, Mode mode,
                    std::uint64_t field_id = 0)
        : AllocatorBase(field_id, "shared", AllocatorKind::Shared),
          name_(std::move(name)), requested_bytes_(total_bytes), mode_(mode) {
        const std::uint64_t kMagic = kSharedMagic;
        const std::size_t kHeader = sizeof(SharedHeader);
#if defined(_WIN32) || defined(_WIN64)
        if (total_bytes < kHeader + 4096) total_bytes = kHeader + 4096;
        total_bytes_ = total_bytes;
        std::wstring wname = widen(name_);
        if (mode_ == Mode::Create) {
            mapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                          static_cast<DWORD>(total_bytes_ >> 32),
                                          static_cast<DWORD>(total_bytes_ & 0xFFFFFFFFu), wname.c_str());
            if (!mapping_) { set_capabilities(basic_caps(false, "CreateFileMapping failed")); return; }
            region_ = static_cast<char*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, total_bytes_));
            if (!region_) { CloseHandle(mapping_); mapping_ = nullptr; set_capabilities(basic_caps(false, "MapViewOfFile failed")); return; }
            SharedHeader* h = reinterpret_cast<SharedHeader*>(region_);
            h->magic = kSharedMagic; h->published_offset = 0; h->published_size = 0; h->next = kHeader;
            init_ok_ = true;
        } else {
            mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname.c_str());
            if (!mapping_) { set_capabilities(basic_caps(false, "OpenFileMapping failed (stale/missing object)")); return; }
            region_ = static_cast<char*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
            if (!region_) { CloseHandle(mapping_); mapping_ = nullptr; set_capabilities(basic_caps(false, "MapViewOfFile failed")); return; }
            SharedHeader* h = reinterpret_cast<SharedHeader*>(region_);
            if (h->magic != kSharedMagic) { UnmapViewOfFile(region_); CloseHandle(mapping_); region_=nullptr; mapping_=nullptr; set_capabilities(basic_caps(false, "shared object magic mismatch (stale/corrupt)")); return; }
            init_ok_ = true;
        }
        (void)kMagic;
#else
        (void)total_bytes; (void)kHeader; (void)kMagic;
        init_ok_ = false;
        set_capabilities(basic_caps(false, "shared memory requires Windows on this build"));
#endif
        if (!init_ok_) {
            AllocatorCapabilities c; c.supports_allocate = false; c.supports_free = false; c.notes = "shared memory unavailable";
            set_capabilities(std::move(c));
            return;
        }
        AllocatorCapabilities c;
        c.supports_allocate = true; c.supports_free = true; c.supports_query = true;
        c.supports_reset = false; c.supports_trim = false;
        c.is_thread_safe = true;
        c.allows_per_object_free = false;
        c.max_alignment = 4096;
        c.max_allocation_size = total_bytes_ - kHeader;
        c.domains = { MemoryDomain::Shared };
        c.notes = "Windows file mapping; arena-like allocation, interprocess";
        set_capabilities(std::move(c));
    }

    ~SharedAllocator() override { shutdown(); }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
#if defined(_WIN32) || defined(_WIN64)
        if (!init_ok_) { r.error = make_error(ErrorCode::init_failed, "shared allocator not initialized"); return r; }
        std::size_t align = req.alignment ? req.alignment : 16;
        if (!is_power_of_two(align) || align > 4096) { r.error = make_error(ErrorCode::alignment_unsupported, "shared supports up to 4096-byte alignment"); return r; }
        std::size_t sz = req.size == 0 ? 1 : req.size;
        std::size_t aligned = align_up(sz, align > 16 ? align : 16);
        SharedHeader* h = reinterpret_cast<SharedHeader*>(region_);
        volatile LONGLONG* next = reinterpret_cast<volatile LONGLONG*>(&h->next);
        for (;;) {
            LONGLONG cur = InterlockedCompareExchange64(next, 0, 0);
            if (cur + static_cast<LONGLONG>(aligned) > static_cast<LONGLONG>(total_bytes_)) {
                r.error = make_error(ErrorCode::capacity_exceeded, "shared region capacity reached");
                ++failures_;
                return r;
            }
            LONGLONG reserve = cur + static_cast<LONGLONG>(aligned);
            if (InterlockedCompareExchange64(next, reserve, cur) == cur) {
                std::uint64_t offset = static_cast<std::uint64_t>(cur);
                void* p = static_cast<void*>(region_ + offset);
                if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(p, sz);
                if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(p, sz, detail::kPoisonAllocByte);
                AllocationId id = ++next_id_;
                live_.emplace(id, Live{ offset, sz, align });
                r.error = Error{}; r.id = id; r.address = p; r.size = req.size; r.alignment = align;
                ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
                live_bytes_ += sz; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
                requested_ += req.size; granted_bytes_ += sz;
                return r;
            }
        }
#else
        (void)req;
        r.error = make_error(ErrorCode::no_backend, "shared memory requires Windows on this build");
        return r;
#endif
    }

    Error free(AllocationHandle handle) override {
        (void)handle;
        return make_error(ErrorCode::not_supported, "shared arena-like allocator does not free individual objects");
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        out.id = handle; out.size = it->second.size; out.granted_size = it->second.size;
        out.alignment = it->second.alignment; out.domain = MemoryDomain::Shared;
        out.address = static_cast<void*>(region_ + it->second.offset); out.live = true;
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
        s.fragmentation.internal_observable = false; s.fragmentation.external_observable = false;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
#if defined(_WIN32) || defined(_WIN64)
        if (region_) { UnmapViewOfFile(region_); region_ = nullptr; }
        if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
#endif
    }

    // --- Two-process proof helpers ---
    char* region() { return region_; }
    std::size_t region_size() const { return total_bytes_; }
    bool initialized() const { return init_ok_; }

    /// Publish a live allocation so a second process can locate it.
    Error publish(AllocationId handle) {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "publish of unknown handle");
        SharedHeader* h = reinterpret_cast<SharedHeader*>(region_);
        h->published_offset = it->second.offset;
        h->published_size = it->second.size;
        return Error{};
    }

    std::uint64_t published_offset() const {
        SharedHeader* h = reinterpret_cast<SharedHeader*>(region_);
        return h->published_offset;
    }
    std::uint64_t published_size() const {
        SharedHeader* h = reinterpret_cast<SharedHeader*>(region_);
        return h->published_size;
    }

private:
    static constexpr std::uint64_t kSharedMagic = 0x53484D454DULL;
    struct SharedHeader { std::uint64_t magic; std::uint64_t published_offset; std::uint64_t published_size; std::uint64_t next; };
    struct Live { std::uint64_t offset; std::size_t size; std::size_t alignment; };

    static std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) & ~(a - 1); }
    static std::wstring widen(const std::string& s) {
        if (s.empty()) return std::wstring();
        std::wstring w(s.size(), L'\0');
        for (std::size_t i = 0; i < s.size(); ++i) w[i] = static_cast<wchar_t>(static_cast<unsigned char>(s[i]));
        return w;
    }
    static AllocatorCapabilities basic_caps(bool alloc, std::string note) {
        AllocatorCapabilities c; c.supports_allocate = alloc; c.supports_free = alloc; c.notes = std::move(note);
        return c;
    }

    mutable std::mutex mutex_;
    std::unordered_map<AllocationId, Live> live_;
    AllocationId next_id_ = 0;
    std::string name_;
    std::size_t requested_bytes_ = 0, total_bytes_ = 0;
    Mode mode_ = Mode::Create;
    bool init_ok_ = false;
#if defined(_WIN32) || defined(_WIN64)
    void* mapping_ = nullptr;
#endif
    char* region_ = nullptr;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_ = 0, granted_bytes_ = 0;
    std::uint64_t success_ = 0, failures_ = 0;
};

} // namespace allocator_lab
