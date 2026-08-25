#pragma once

// Allocator Lab 1.0.0
// Allocator capability discovery.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "allocator_lab/types.hpp"
#include "allocator_lab/config.hpp"

namespace allocator_lab {

/// Explicit, honest description of which operations a strategy supports.
///
/// Capabilities are self-reported by strategies and are the lab's single source
/// of truth about what the experiment runner may invoke. Unsupported operations
/// must return a structured "not_supported" error; the lab never fabricates
/// success for an operation the backend does not implement.
struct AllocatorCapabilities {
    // Core operations
    bool supports_allocate = true;
    bool supports_free = true;
    bool supports_reallocate = false;
    bool supports_query = true;
    bool supports_trim = false;
    bool supports_reset = false;

    // Memory-management operations
    bool supports_reserve = false;
    bool supports_commit = false;
    bool supports_decommit = false;
    bool supports_prewarm = false;
    bool supports_purge = false;
    bool supports_map = false;
    bool supports_unmap = false;
    bool supports_import_export = false;

    // Semantic traits
    bool is_thread_safe = false;
    bool is_single_thread_only = false;
    bool is_experimental = false;
    bool allows_per_object_free = true;
    bool supports_alignment = false;
    bool supports_zeroing_policy = false;

    // Limits
    std::size_t max_alignment = 1;
    std::size_t max_allocation_size = 0;   // 0 => governed by lab Bounds

    std::vector<MemoryDomain> domains;
    std::vector<ZeroingPolicy> zeroing_policies;
    std::string notes;
};

} // namespace allocator_lab
