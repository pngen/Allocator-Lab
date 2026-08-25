#pragma once

// Allocator Lab 1.0.0
// Measurement value types and collectors.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

namespace allocator_lab {

/// Summary of a latency distribution measured in nanoseconds.
///
/// When raw samples were collected, percentiles are exact (from the sorted
/// sample set). Otherwise they are estimated from a bounded log2 histogram so
/// samples can never grow unbounded.
struct LatencySummary {
    std::uint64_t n = 0;
    double mean = 0.0;
    double min = 0.0;
    double max = 0.0;
    double p50 = 0.0;
    double p90 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double p999 = 0.0;
};

/// Non-thread-safe latency collector. One should be used per worker and merged
/// afterwards. Preserves a bounded number of raw samples (for exact percentiles)
/// and always maintains a log2 histogram (for bounded-memory estimates).
class LatencyCollector {
public:
    explicit LatencyCollector(std::uint64_t max_raw_samples = 4ull * 1024 * 1024)
        : max_raw_samples_(max_raw_samples) {}

    void add(std::uint64_t ns) noexcept {
        ++n_;
        sum_ += static_cast<double>(ns);
        if (ns < min_) min_ = ns;
        if (ns > max_) max_ = ns;
        // log2 bucketing
        std::uint8_t bucket = bucket_for(ns);
        ++hist_[bucket];
        if (raw_.size() < max_raw_samples_) raw_.push_back(ns);
    }

    LatencySummary summary() const {
        LatencySummary s;
        s.n = n_;
        if (n_ == 0) return s;
        s.mean = sum_ / static_cast<double>(n_);
        s.min = static_cast<double>(min_);
        s.max = static_cast<double>(max_);
        s.p50 = percentile(0.50);
        s.p90 = percentile(0.90);
        s.p95 = percentile(0.95);
        s.p99 = percentile(0.99);
        s.p999 = percentile(0.999);
        return s;
    }

    std::uint64_t count() const noexcept { return n_; }

    /// Merge another collector's stats into this one (used to combine workers).
    void merge(const LatencyCollector& other) {
        n_ += other.n_;
        sum_ += other.sum_;
        min_ = std::min(min_, other.min_);
        max_ = std::max(max_, other.max_);
        raw_.reserve(raw_.size() + other.raw_.size());
        raw_.insert(raw_.end(), other.raw_.begin(), other.raw_.end());
        if (raw_.size() > max_raw_samples_) raw_.resize(max_raw_samples_);
        for (std::size_t i = 0; i < 64; ++i) hist_[i] += other.hist_[i];
    }

private:
    static std::uint8_t bucket_for(std::uint64_t ns) noexcept {
        std::uint8_t b = 0;
        while (b < 63 && (std::uint64_t{1} << b) <= ns) ++b;
        return b;
    }

    double percentile(double p) const {
        if (raw_.size() >= n_ && !raw_.empty()) {
            std::vector<std::uint64_t> sorted = raw_;
            std::sort(sorted.begin(), sorted.end());
            return sample_at(sorted, p);
        }
        // histogram estimate
        std::uint64_t target = static_cast<std::uint64_t>(
            std::ceil(p * static_cast<double>(n_)));
        if (target == 0) target = 1;
        std::uint64_t cum = 0;
        for (std::size_t b = 0; b < 64; ++b) {
            cum += hist_[b];
            if (cum >= target) return static_cast<double>(std::uint64_t{1} << b);
        }
        return static_cast<double>(max_);
    }

    static double sample_at(const std::vector<std::uint64_t>& sorted, double p) {
        if (sorted.empty()) return 0.0;
        double idx = p * static_cast<double>(sorted.size() - 1);
        std::size_t lo = static_cast<std::size_t>(idx);
        std::size_t hi = std::min(lo + 1, sorted.size() - 1);
        double frac = idx - static_cast<double>(lo);
        return static_cast<double>(sorted[lo]) * (1.0 - frac) +
               static_cast<double>(sorted[hi]) * frac;
    }

    std::uint64_t n_ = 0;
    double sum_ = 0.0;
    std::uint64_t min_ = UINT64_MAX;
    std::uint64_t max_ = 0;
    std::uint64_t raw_only_ = 0;
    std::uint64_t hist_[64] = {};
    std::vector<std::uint64_t> raw_;
    std::uint64_t max_raw_samples_;
};

/// Throughput counters, measured in completed operations / bytes.
struct ThroughputSummary {
    std::uint64_t allocations = 0;
    std::uint64_t frees = 0;
    std::uint64_t reallocations = 0;
    std::uint64_t operations = 0;
    std::uint64_t bytes_allocated = 0;
    std::uint64_t bytes_freed = 0;
    double elapsed_seconds = 0.0;

    double allocs_per_sec() const {
        return elapsed_seconds > 0 ? static_cast<double>(allocations) / elapsed_seconds : 0.0;
    }
    double frees_per_sec() const {
        return elapsed_seconds > 0 ? static_cast<double>(frees) / elapsed_seconds : 0.0;
    }
    double ops_per_sec() const {
        return elapsed_seconds > 0 ? static_cast<double>(operations) / elapsed_seconds : 0.0;
    }
    double bytes_per_sec_alloc() const {
        return elapsed_seconds > 0 ? static_cast<double>(bytes_allocated) / elapsed_seconds : 0.0;
    }
};

/// Accounting counters tracking requested/granted/reused bytes.
struct AccountingCounters {
    std::uint64_t total_requested_bytes = 0;   // sum of requested sizes
    std::uint64_t total_granted_bytes = 0;     // sum of granted sizes
    std::uint64_t internal_waste = 0;          // granted - requested
    std::uint64_t alignment_waste = 0;         // padding to satisfy alignment
    std::uint64_t metadata_bytes = 0;          // allocator metadata, if measurable
    std::uint64_t reused_bytes = 0;            // bytes served from reused backing
    std::uint64_t fresh_bytes = 0;             // bytes served from fresh backing
    std::uint64_t released_bytes = 0;          // bytes returned to OS
};

/// Fragmentation metrics. Metrics that a backend cannot observe are left at the
/// sentinel 0 and the report "unsupported" explicitly; nothing is fabricated.
struct FragmentationMetrics {
    double internal_fragmentation = 0.0;   // waste / granted, [0,1]
    double external_fragmentation = 0.0;   // 1 - largest_free / total_free where measureable
    std::uint64_t largest_free_span = 0;   // largest contiguous free block
    std::uint64_t free_span_count = 0;
    std::uint64_t free_bytes = 0;
    std::uint64_t reserved_bytes = 0;
    std::uint64_t committed_bytes = 0;
    std::uint64_t live_bytes = 0;
    bool internal_observable = true;
    bool external_observable = true;
    bool backing_observable = true;

    double largest_over_free_ratio() const {
        return free_bytes > 0
            ? static_cast<double>(largest_free_span) / static_cast<double>(free_bytes)
            : 0.0;
    }
    double reserved_over_live_ratio() const {
        return live_bytes > 0 ? static_cast<double>(reserved_bytes) / static_cast<double>(live_bytes) : 0.0;
    }
    double committed_over_live_ratio() const {
        return live_bytes > 0 ? static_cast<double>(committed_bytes) / static_cast<double>(live_bytes) : 0.0;
    }
};

/// Reuse / pool behavior metrics.
struct ReuseMetrics {
    std::uint64_t pool_hits = 0;
    std::uint64_t fresh_allocations = 0;
    std::uint64_t reuse_count = 0;
    std::uint64_t reuse_bytes = 0;
    std::uint64_t same_size_reuse = 0;
    std::uint64_t cross_class_reuse = 0;
    double average_reuse_distance = 0.0;
    double reuse_lifetime_seconds = 0.0;
    std::uint64_t trim_recovery_bytes = 0;
    std::uint64_t backing_retained_after_workload = 0;

    double reuse_rate() const {
        const double total = static_cast<double>(pool_hits + fresh_allocations);
        return total > 0 ? static_cast<double>(pool_hits) / total : 0.0;
    }
};

/// Live-state counters.
struct LiveState {
    std::uint64_t live_count = 0;
    std::uint64_t live_bytes = 0;
    std::uint64_t peak_live_count = 0;
    std::uint64_t peak_live_bytes = 0;
    std::uint64_t reserved = 0;
    std::uint64_t committed = 0;
    std::uint64_t peak_reserved = 0;
    std::uint64_t peak_committed = 0;
};

/// Aggregate statistics reported by an allocator.
struct AllocatorStatistics {
    AllocatorId allocator_id = 0;
    std::string name;
    LiveState live;
    AccountingCounters accounting;
    FragmentationMetrics fragmentation;
    ReuseMetrics reuse;
    std::uint64_t failure_count = 0;
    std::uint64_t success_count = 0;
    std::uint64_t split_count = 0;    // buddy/slab split events
    std::uint64_t merge_count = 0;    // buddy/freelist coalesce events
    std::uint64_t growth_events = 0;  // pool/arena backing growth events
    std::uint64_t search_depth_total = 0; // freelist search cost
};

} // namespace allocator_lab
