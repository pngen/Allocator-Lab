#pragma once

// Allocator Lab 1.0.0
// File-backed / memory-mapped allocator (real Windows file mapping).
//
// Arena-like bump allocation inside a memory-mapped file. Setup (create/map)
// latency is recorded separately from allocation cost so the two can be
// reported independently.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <chrono>
#include <functional>
#include <string>

#include "allocator_lab/detail/allocator_base.hpp"
#include "allocator_lab/detail/platform.hpp"
#include "allocator_lab/config.hpp"

#if defined(_WIN32) || defined(_WIN64)
  #include <windows.h>
#endif

namespace allocator_lab {

class MappedAllocator : public detail::AllocatorBase {
public:
    MappedAllocator(std::string path, std::size_t total_bytes, bool create,
                    std::uint64_t field_id = 0)
        : AllocatorBase(field_id, "mapped", AllocatorKind::Mapped),
          path_(std::move(path)), requested_bytes_(total_bytes) {
        const std::size_t kHeader = sizeof(MappedHeader);
#if defined(_WIN32) || defined(_WIN64)
        if (total_bytes < kHeader + 4096) total_bytes = kHeader + 4096;
        total_bytes_ = total_bytes;
        auto t0 = std::chrono::steady_clock::now();
        std::wstring wpath = widen(path_);
        std::wstring wname = L"Local\\allocator_lab_mmap_" + std::to_wstring(std::hash<std::string>{}(path_));
        if (create) {
            file_ = CreateFileW(wpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file_ != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(total_bytes_);
                SetFilePointerEx(file_, li, nullptr, FILE_BEGIN); SetEndOfFile(file_);
                DWORD hb = static_cast<DWORD>(total_bytes_ >> 32), lb = static_cast<DWORD>(total_bytes_ & 0xFFFFFFFFu);
                mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READWRITE, hb, lb, wname.c_str());
            }
            if (mapping_) {
                region_ = static_cast<char*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, total_bytes_));
                if (region_) {
                    MappedHeader* h = reinterpret_cast<MappedHeader*>(region_);
                    h->magic = kMappedMagic; h->next = kHeader;
                    init_ok_ = true;
                }
            }
        } else {
            mapping_ = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wname.c_str());
            if (!mapping_) { file_ = CreateFileW(wpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (file_ != INVALID_HANDLE_VALUE) { DWORD hb = static_cast<DWORD>(total_bytes_ >> 32), lb = static_cast<DWORD>(total_bytes_ & 0xFFFFFFFFu); mapping_ = CreateFileMappingW(file_, nullptr, PAGE_READWRITE, hb, lb, wname.c_str()); } }
            if (mapping_) {
                region_ = static_cast<char*>(MapViewOfFile(mapping_, FILE_MAP_ALL_ACCESS, 0, 0, 0));
                if (region_) { MappedHeader* h = reinterpret_cast<MappedHeader*>(region_); if (h->magic != kMappedMagic) { UnmapViewOfFile(region_); region_ = nullptr; } else init_ok_ = true; }
            }
        }
        setup_latency_ns_ = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count());
#else
        (void)total_bytes; (void)kHeader;
#endif
        AllocatorCapabilities c;
        c.supports_allocate = init_ok_; c.supports_free = init_ok_; c.supports_query = true;
        c.is_thread_safe = true; c.allows_per_object_free = false;
        c.max_alignment = 4096; c.max_allocation_size = total_bytes_ - kHeader;
        c.domains = { MemoryDomain::Mapped };
        c.notes = init_ok_ ? "file-backed mapping; arena-like allocation" : "mapped memory unavailable";
        set_capabilities(std::move(c));
    }

    ~MappedAllocator() override { shutdown(); }

    AllocationResult allocate(const AllocationRequest& req) override {
        std::lock_guard lock(mutex_);
        AllocationResult r;
#if defined(_WIN32) || defined(_WIN64)
        if (!init_ok_) { r.error = make_error(ErrorCode::init_failed, "mapped allocator not initialized"); return r; }
        std::size_t align = req.alignment ? req.alignment : 16;
        if (!is_power_of_two(align) || align > 4096) { r.error = make_error(ErrorCode::alignment_unsupported, "mapped supports up to 4096-byte alignment"); return r; }
        std::size_t sz = req.size == 0 ? 1 : req.size;
        std::size_t aligned = align_up(sz, align > 16 ? align : 16);
        MappedHeader* h = reinterpret_cast<MappedHeader*>(region_);
        volatile LONGLONG* next = reinterpret_cast<volatile LONGLONG*>(&h->next);
        for (;;) {
            LONGLONG cur = InterlockedCompareExchange64(next, 0, 0);
            if (cur + static_cast<LONGLONG>(aligned) > static_cast<LONGLONG>(total_bytes_)) { r.error = make_error(ErrorCode::capacity_exceeded, "mapped region capacity reached"); ++failures_; return r; }
            LONGLONG reserve = cur + static_cast<LONGLONG>(aligned);
            if (InterlockedCompareExchange64(next, reserve, cur) == cur) {
                void* p = static_cast<void*>(region_ + static_cast<std::uint64_t>(cur));
                if (any(req.flags & AllocationFlags::ZeroFill)) detail::fill_zero(p, sz);
                if (any(req.flags & AllocationFlags::PoisonAlloc)) detail::fill_poison(p, sz, detail::kPoisonAllocByte);
                AllocationId id = ++next_id_;
                live_.emplace(id, Live{ static_cast<std::uint64_t>(cur), sz, align });
                r.error = Error{}; r.id = id; r.address = p; r.size = req.size; r.alignment = align;
                ++success_; ++live_count_; if (live_count_ > peak_live_) peak_live_ = live_count_;
                live_bytes_ += sz; if (live_bytes_ > peak_bytes_) peak_bytes_ = live_bytes_;
                requested_ += req.size; granted_bytes_ += sz;
                return r;
            }
        }
#else
        (void)req;
        r.error = make_error(ErrorCode::no_backend, "mapped memory requires Windows on this build");
        return r;
#endif
    }

    Error free(AllocationHandle handle) override {
        (void)handle;
        return make_error(ErrorCode::not_supported, "mapped arena-like allocator does not free individual objects");
    }

    Error query(AllocationHandle handle, AllocationInfo& out) override {
        std::lock_guard lock(mutex_);
        auto it = live_.find(handle);
        if (it == live_.end()) return make_error(ErrorCode::invalid_handle, "query of unknown handle");
        out.id = handle; out.size = it->second.size; out.granted_size = it->second.size;
        out.alignment = it->second.alignment; out.domain = MemoryDomain::Mapped;
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
        s.accounting.total_requested_bytes = requested_; s.accounting.total_granted_bytes = granted_bytes_;
        s.fragmentation.internal_observable = false; s.fragmentation.external_observable = false;
        s.success_count = success_; s.failure_count = failures_;
        return s;
    }

    void shutdown() noexcept override {
        std::lock_guard lock(mutex_);
#if defined(_WIN32) || defined(_WIN64)
        if (region_) { UnmapViewOfFile(region_); region_ = nullptr; }
        if (mapping_) { CloseHandle(mapping_); mapping_ = nullptr; }
        if (file_ != INVALID_HANDLE_VALUE) { CloseHandle(file_); file_ = INVALID_HANDLE_VALUE; }
#endif
    }

    char* region() { return region_; }
    std::size_t region_size() const { return total_bytes_; }
    bool initialized() const { return init_ok_; }
    std::uint64_t setup_latency_ns() const { return setup_latency_ns_; }

private:
    static constexpr std::uint64_t kMappedMagic = 0x4D4150504DULL;
    struct MappedHeader { std::uint64_t magic; std::uint64_t next; };
    struct Live { std::uint64_t offset; std::size_t size; std::size_t alignment; };
    static std::size_t align_up(std::size_t v, std::size_t a) { return (v + a - 1) & ~(a - 1); }
    static std::wstring widen(const std::string& s) {
        std::wstring w(s.size(), L'\0');
        for (std::size_t i = 0; i < s.size(); ++i) w[i] = static_cast<wchar_t>(static_cast<unsigned char>(s[i]));
        return w;
    }

    mutable std::mutex mutex_;
    std::unordered_map<AllocationId, Live> live_;
    AllocationId next_id_ = 0;
    std::string path_;
    std::size_t requested_bytes_ = 0, total_bytes_ = 0;
    bool init_ok_ = false;
    std::uint64_t setup_latency_ns_ = 0;
#if defined(_WIN32) || defined(_WIN64)
    void* file_ = INVALID_HANDLE_VALUE;
    void* mapping_ = nullptr;
#endif
    char* region_ = nullptr;
    std::uint64_t live_count_ = 0, live_bytes_ = 0, peak_live_ = 0, peak_bytes_ = 0;
    std::uint64_t requested_ = 0, granted_bytes_ = 0;
    std::uint64_t success_ = 0, failures_ = 0;
};

} // namespace allocator_lab
