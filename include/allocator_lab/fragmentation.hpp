#pragma once

// Allocator Lab 1.0.0
// Fragmentation measurement and honest "unsupported" labeling.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <string>

#include "allocator_lab/metrics.hpp"

namespace allocator_lab {

/// Build a fragmentation metric snapshot from free-block observables.
///
/// external_fragmentation is defined as 1 - largest_free_span / free_bytes,
/// i.e. how much of the free space is unusable as a single contiguous block.
/// It is only meaningful when the allocator exposes a free list / free span
/// structure. When a backend cannot observe this structure, pass
/// external_observable = false so the report marks it unsupported instead of
/// emitting a fabricated number.
inline FragmentationMetrics make_fragmentation(
    std::uint64_t free_bytes,
    std::uint64_t largest_free_span,
    std::uint64_t free_span_count,
    bool external_observable,
    std::uint64_t live_bytes = 0,
    std::uint64_t reserved_bytes = 0,
    std::uint64_t committed_bytes = 0) {

    FragmentationMetrics m;
    m.free_bytes = free_bytes;
    m.largest_free_span = largest_free_span;
    m.free_span_count = free_span_count;
    m.external_observable = external_observable;
    m.live_bytes = live_bytes;
    m.reserved_bytes = reserved_bytes;
    m.committed_bytes = committed_bytes;
    if (free_bytes > 0 && external_observable) {
        m.external_fragmentation =
            1.0 - (static_cast<double>(largest_free_span) / static_cast<double>(free_bytes));
        if (m.external_fragmentation < 0.0) m.external_fragmentation = 0.0;
    }
    m.internal_fragmentation =
        (reserved_bytes > 0)
            ? 1.0 - (static_cast<double>(live_bytes) / static_cast<double>(reserved_bytes))
            : 0.0;
    return m;
}

/// Produce a fragmentation metric marked unsupported for the given reason.
inline FragmentationMetrics unsupported_fragmentation(std::string reason) {
    FragmentationMetrics m;
    m.internal_observable = false;
    m.external_observable = false;
    m.backing_observable = false;
    m.free_bytes = 0;
    m.largest_free_span = 0;
    m.free_span_count = 0;
    (void)reason;
    return m;
}

} // namespace allocator_lab
