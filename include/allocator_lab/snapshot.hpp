#pragma once

// Allocator Lab 1.0.0
// Immutable experiment snapshots.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <string>

#include "allocator_lab/types.hpp"
#include "allocator_lab/metrics.hpp"
#include "allocator_lab/capabilities.hpp"
#include "allocator_lab/provenance.hpp"

namespace allocator_lab {

/// An immutable, deterministic snapshot of experiment state at a step.
/// Produced by the experiment runner; ordering is stable.
struct ExperimentSnapshot {
    SnapshotId snapshot_id = 0;
    ExperimentId experiment_id = 0;
    AllocatorId allocator_id = 0;
    std::string allocator_name;
    std::string allocator_config;      // serialized config
    std::uint64_t step = 0;            // operations so far
    std::uint64_t seed = 0;

    LiveState live;
    FragmentationMetrics fragmentation;
    AccountingCounters accounting;
    ReuseMetrics reuse;
    ThroughputSummary throughput;

    std::uint64_t success_count = 0;
    std::uint64_t failure_count = 0;
    std::uint64_t capacity_reached_count = 0;

    std::string trace_position;
    AllocatorCapabilities capabilities;
    BuildProvenance provenance;
};

} // namespace allocator_lab
