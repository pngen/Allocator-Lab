#pragma once

// Allocator Lab 1.0.0
// Workload configuration, size/lifetime distributions, and deterministic generation.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "allocator_lab/types.hpp"
#include "allocator_lab/trace.hpp"
#include "allocator_lab/error.hpp"

namespace allocator_lab {

// -------------------------------------------------------------------------
// Distributions
// -------------------------------------------------------------------------

enum class SizeDistribution : std::uint8_t {
    Fixed = 0,
    Uniform = 1,
    LogUniform = 2,
    Geometric = 3,
    Bimodal = 4,
    MultiModal = 5,
    PowerLaw = 6,
    Histogram = 7,
    CallerSequence = 8,
};

enum class LifetimeDistribution : std::uint8_t {
    Immediate = 0,
    Short = 1,
    Medium = 2,
    Long = 3,
    Mixed = 4,
    Fixed = 5,
    Random = 6,
    PhaseBound = 7,
    CallerProvided = 8,
};

enum class AlignmentDistribution : std::uint8_t {
    Natural = 0,
    Fixed = 1,
    UniformPow2 = 2,
};

// Free-order policy for the generator's live set.
enum class FreeOrder : std::uint8_t {
    Fifo = 0,
    Lifo = 1,
    Random = 2,
    ByLifetime = 3,
};

// -------------------------------------------------------------------------
// Workload configuration
// -------------------------------------------------------------------------

struct WorkloadConfig {
    std::string name;

    // Operation budget: number of trace entries the generator emits.
    std::uint64_t operations = 100000;
    // Target peak live allocations the generator steers toward.
    std::uint64_t live_target = 1024;

    // Size range and distribution.
    std::size_t min_size = 16;
    std::size_t max_size = 8 * 1024 * 1024;
    SizeDistribution size_dist = SizeDistribution::LogUniform;
    std::vector<std::size_t> size_histogram;   // weights for Histogram
    std::vector<std::size_t> size_sequence;    // for CallerSequence
    std::vector<std::size_t> multimodal_peaks; // for MultiModal

    // Alignment.
    AlignmentDistribution alignment_dist = AlignmentDistribution::Natural;
    std::vector<std::size_t> alignments;       // for Fixed alignment set

    // Operation mix.
    double alloc_free_ratio = 1.0;   // allocations per free (>= 0)
    double realloc_fraction = 0.05;  // fraction of steps that reallocate
    double burstiness = 0.0;         // 0..1: extra allocations in bursts

    // Lifetime.
    LifetimeDistribution lifetime_dist = LifetimeDistribution::Short;
    std::uint64_t fixed_lifetime_steps = 256;
    std::uint64_t lifetime_min = 16;
    std::uint64_t lifetime_max = 4096;
    std::uint64_t short_mean = 64;
    std::uint64_t medium_mean = 512;
    std::uint64_t long_mean = 8192;

    // Reuse / locality.
    double temporal_locality = 0.0;    // prob of reusing recent size class
    double object_reuse_prob = 0.0;    // prob of reusing a freed id
    std::uint32_t hot_class_count = 8;
    std::uint32_t cold_class_count = 4;

    // Domain / concurrency.
    MemoryDomain domain = MemoryDomain::Host;
    std::uint32_t threads = 1;
    std::uint32_t producer_consumer_count = 0;
    std::uint64_t working_set_target = 0;
    FreeOrder free_order = FreeOrder::ByLifetime;

    // Phases (fractions of operations, summed should be <= 1.0).
    double warmup_fraction = 0.10;
    double cooldown_fraction = 0.10;
    bool release_to_zero = true;

    // Determinism.
    std::uint64_t seed = 0x9E3779B97F4A7C15ULL;
};

// -------------------------------------------------------------------------
// Generator
// -------------------------------------------------------------------------

// Validate and fill defaults. Returns the sanitized config, or an error.
Error sanitize_workload_config(const WorkloadConfig& in, WorkloadConfig& out);

// Deterministically generate an allocation trace from a validated config.
// Output trace seed is the config seed; generation is fully reproducible.
Error generate_workload(const WorkloadConfig& config, Trace& out);

// Stable one-line description of a config (for provenance / reports).
std::string workload_config_to_string(const WorkloadConfig& config);

} // namespace allocator_lab
