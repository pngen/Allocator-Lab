#pragma once

// Allocator Lab 1.0.0
// Experiment config, result, and runner surface.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <string>
#include <vector>

#include "allocator_lab/types.hpp"
#include "allocator_lab/metrics.hpp"
#include "allocator_lab/snapshot.hpp"
#include "allocator_lab/capabilities.hpp"
#include "allocator_lab/provenance.hpp"
#include "allocator_lab/trace.hpp"
#include "allocator_lab/allocator.hpp"

namespace allocator_lab {

/// Configuration for running a trace against an allocator.
struct ExperimentConfig {
    std::uint64_t warmup_ops = 0;        // skip this many ops from timing
    std::uint64_t iterations = 1;        // repeat the trace this many times
    std::uint64_t snapshot_interval = 0; // snapshot every N recorded ops (0 = none)
    bool collect_raw_latency = true;
    std::uint64_t max_raw_samples = 4ull * 1024 * 1024;
    bool verify_payload = false;         // fill + verify deterministic patterns
    std::uint32_t threads = 1;           // 1 = strictly sequential deterministic replay
    bool sync_after_run = true;          // for async/completed-work backends
};

/// Per-worker aggregate outcome.
struct WorkerResult {
    WorkerId worker = 0;
    std::uint64_t ops = 0;
    std::uint64_t successes = 0;
    std::uint64_t failures = 0;
    LatencySummary alloc_latency;
    LatencySummary free_latency;
    LatencySummary realloc_latency;
    double elapsed_ns = 0.0;
};

/// The measured outcome of running a workload against one allocator.
struct ExperimentResult {
    ExperimentId experiment_id = 0;
    AllocatorId allocator_id = 0;
    std::string allocator_name;
    AllocatorKind kind = AllocatorKind::System;
    AllocatorCapabilities capabilities;
    std::uint64_t operations = 0;
    std::uint64_t seed = 0;
    std::uint64_t success_count = 0;
    std::uint64_t failure_count = 0;
    LiveState live;
    AccountingCounters accounting;
    FragmentationMetrics fragmentation;
    ReuseMetrics reuse;
    LatencySummary alloc_latency;
    LatencySummary free_latency;
    LatencySummary realloc_latency;
    ThroughputSummary throughput;
    std::uint64_t elapsed_ns = 0;
    std::vector<ExperimentSnapshot> snapshots;
    std::vector<WorkerResult> workers;
    BuildProvenance provenance;
};

/// Run a trace against an allocator as a strictly-sequential, deterministic
/// replay (threads must be 1). Measures latency/throughput and records snapshots.
ExperimentResult run_replay(AllocatorStrategy& allocator, const Trace& trace,
                            const ExperimentConfig& config, BuildProvenance provenance);

/// Run a trace concurrently. The trace is partitioned by its worker field; each
/// worker stream runs in its own thread. The allocator must be thread-safe.
ExperimentResult run_concurrent(AllocatorStrategy& allocator, const Trace& trace,
                                const ExperimentConfig& config, std::uint32_t threads,
                                BuildProvenance provenance);

} // namespace allocator_lab
