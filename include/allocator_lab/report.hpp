#pragma once

// Allocator Lab 1.0.0
// Report generation, comparison engine, and explainability.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

#include "allocator_lab/experiment.hpp"

namespace allocator_lab {

/// One allocator's normalized row in a comparison.
struct ComparisonEntry {
    AllocatorId id = 0;
    std::string name;
    AllocatorKind kind = AllocatorKind::System;
    double ops_per_sec = 0.0;
    double alloc_mean_ns = 0.0, alloc_p95 = 0.0, alloc_p99 = 0.0;
    double free_mean_ns = 0.0;
    double realloc_mean_ns = 0.0;
    std::uint64_t success = 0, failure = 0;
    double success_rate = 0.0;
    std::uint64_t peak_live_bytes = 0, peak_reserved = 0;
    double internal_waste = 0.0;
    double external_fragmentation = 0.0;
    double reuse_rate = 0.0;
    std::uint64_t retained_bytes = 0;
    std::uint64_t growth_events = 0;
    std::uint64_t trim_recovery = 0;
    double weighted_score = 0.0;
    bool has_weighted_score = false;
};

/// A weighted ranking dimension. Weights are caller-configured; there is never
/// an implicit universal score.
struct WeightDimension { std::string name; double weight; };

struct ComparisonReport {
    std::vector<ComparisonEntry> entries;
    std::vector<WeightDimension> weights;
    bool has_weighted_score = false;
    std::string workload_desc;
};

/// Build a normalized comparison from experiment results. Order follows input.
ComparisonReport build_comparison(const std::vector<ExperimentResult>& results);

/// Attach a caller-configured weighted score. Higher weight -> more important.
/// Score is normalized per dimension to [0,1] by best-in-set; higher is better.
void apply_weighted_score(ComparisonReport& report);

// Human-readable / machine-readable emission.
std::string report_to_text(const ComparisonReport& report);
std::string report_to_json(const ComparisonReport& report);
std::string report_to_csv(const ComparisonReport& report);

// Per-result emission.
std::string result_to_text(const ExperimentResult& result);
std::string result_to_json(const ExperimentResult& result);
std::string result_to_csv_header();
std::string result_to_csv_row(const ExperimentResult& result);

// Explainability: measured facts plus clearly-labeled heuristic interpretation.
std::string explain_comparison(const std::vector<ExperimentResult>& results, const std::string& workload_desc);

Error write_text_file(const std::filesystem::path& path, const std::string& content);

} // namespace allocator_lab
