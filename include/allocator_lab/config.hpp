#pragma once

// Allocator Lab 1.0.0
// Configuration policies, bounded limits, and validation helpers.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "allocator_lab/error.hpp"

namespace allocator_lab {

// ---------------------------------------------------------------------------
// Policies
// ---------------------------------------------------------------------------

/// How size classes are selected for size-class allocators.
enum class SizeClassPolicy : std::uint8_t {
    PowerOfTwo = 0,    ///< Round up to next power of two.
    FixedTable = 1,    ///< Use an explicit, caller-supplied table.
};

/// How alignment is handled by a strategy.
enum class AlignmentPolicy : std::uint8_t {
    Natural = 0,       ///< Follow allocator natural alignment only.
    Requested = 1,     ///< Honour the explicit request if legality permits.
    PowerOfTwoOnly = 2,///< Reject non-power-of-two alignment requests.
};

/// How an allocator grows or shrinks its backing.
enum class GrowthPolicy : std::uint8_t {
    Doubling = 0,
    Linear = 1,
    Fixed = 2,
    Manual = 3,
};

/// How backing is reused vs freshly acquired.
enum class ReusePolicy : std::uint8_t {
    PreferReuse = 0,
    PreferFresh = 1,
    DemandOnly = 2,
};

/// What an allocator does when it reaches a configured limit.
enum class FailurePolicy : std::uint8_t {
    Fail = 0,            ///< Return a structured capacity error.
    Grow = 1,            ///< Attempt to grow backing; if impossible, fail.
    RetryOnce = 2,       ///< Attempt one backing growth, else fail.
};

/// Hard bounds applied by the lab so that no external input can drive an
/// unbounded vector/queue. Zero means "default" and is replaced by a sane
/// default during validation; there is never an implicit "unlimited".
struct Bounds {
    std::uint64_t max_allocation_size = 1ull << 33;   // 8 GiB guard
    std::uint64_t max_total_live_bytes = 1ull << 36;  // 64 GiB guard
    std::uint64_t max_total_reserved_bytes = 1ull << 37;
    std::uint64_t max_live_allocation_count = 64ull * 1024 * 1024;
    std::uint64_t max_trace_entries = 128ull * 1024 * 1024;
    std::uint64_t max_report_samples = 16ull * 1024 * 1024;
    std::uint64_t max_threads = 1024;
    std::uint64_t max_allocators_per_comparison = 256;
    std::uint64_t max_metadata_bytes = 1ull << 31;        // 2 GiB
    std::uint64_t max_event_queue = 16ull * 1024 * 1024;
    std::uint64_t max_histogram_buckets = 4096;
};

// ---------------------------------------------------------------------------
// Validation utilities
// ---------------------------------------------------------------------------

constexpr bool is_power_of_two(std::uint64_t v) noexcept {
    return v != 0 && (v & (v - 1)) == 0;
}

constexpr std::uint64_t round_up_pow2(std::uint64_t v) noexcept {
    if (v <= 1) return 1;
    --v;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16; v |= v >> 32;
    return v + 1;
}

constexpr std::uint64_t log2_floor(std::uint64_t v) noexcept {
    std::uint64_t r = 0;
    while (v > 1) { v >>= 1; ++r; }
    return r;
}

constexpr std::uint64_t log2_ceil(std::uint64_t v) noexcept {
    if (v <= 1) return 0;
    return log2_floor(v - 1) + 1;
}

/// Validate an alignment request. Returns invalid_alignment / overflow.
inline Error validate_alignment(std::size_t alignment) noexcept {
    if (alignment == 0) return make_error(ErrorCode::invalid_alignment,
        "alignment must be non-zero");
    if (!is_power_of_two(alignment)) return make_error(ErrorCode::invalid_alignment,
        "alignment must be a power of two");
    return Error{};
}

} // namespace allocator_lab
