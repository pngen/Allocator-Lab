#pragma once

// Allocator Lab 1.0.0
// Platform memory helpers: system + aligned allocation, zero/poison fill.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
// Windows aligned allocation
#if defined(_WIN32) || defined(_WIN64)
  #include <malloc.h>
  #include <windows.h>
#else
  #include <cstdlib>
  #include <unistd.h>
#endif

namespace allocator_lab::detail {

inline void* system_alloc(std::size_t size) { return std::malloc(size); }
inline void system_free(void* p) noexcept { std::free(p); }

// Allocate with explicit alignment. align must be a power of two.
inline void* aligned_alloc(std::size_t size, std::size_t align) {
#if defined(_WIN32) || defined(_WIN64)
    return _aligned_malloc(size, align);
#else
    void* p = nullptr;
    if (posix_memalign(&p, align, size) != 0) return nullptr;
    return p;
#endif
}

inline void aligned_free(void* p) noexcept {
#if defined(_WIN32) || defined(_WIN64)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

// Natural alignment used by std::malloc on this platform.
constexpr std::size_t default_alignment = alignof(std::max_align_t);

inline void fill_zero(void* p, std::size_t n) noexcept {
    if (p && n) std::memset(p, 0, n);
}

// A deterministic non-zero poison pattern, used to expose stale reuse.
constexpr std::uint8_t kPoisonAllocByte = 0xAB;
constexpr std::uint8_t kPoisonFreeByte = 0xCD;
constexpr std::uint8_t kCanaryByte = 0x77;

inline void fill_poison(void* p, std::size_t n, std::uint8_t b) noexcept {
    if (p && n) std::memset(p, static_cast<int>(b), n);
}

} // namespace allocator_lab::detail
