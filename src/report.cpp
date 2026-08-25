// Allocator Lab 1.0.0
// Report generation, comparison engine, and explainability.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "allocator_lab/report.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace allocator_lab {

ComparisonReport build_comparison(const std::vector<ExperimentResult>& results) {
    ComparisonReport report;
    for (const auto& r : results) {
        ComparisonEntry e;
        e.id = r.allocator_id; e.name = r.allocator_name; e.kind = r.kind;
        e.ops_per_sec = r.throughput.ops_per_sec();
        e.alloc_mean_ns = r.alloc_latency.mean;
        e.alloc_p95 = r.alloc_latency.p95;
        e.alloc_p99 = r.alloc_latency.p99;
        e.free_mean_ns = r.free_latency.mean;
        e.realloc_mean_ns = r.realloc_latency.mean;
        e.success = r.success_count; e.failure = r.failure_count;
        e.success_rate = (r.success_count + r.failure_count) > 0 ?
            static_cast<double>(r.success_count) / static_cast<double>(r.success_count + r.failure_count) : 0.0;
        e.peak_live_bytes = r.live.peak_live_bytes;
        e.peak_reserved = r.live.peak_reserved;
        e.internal_waste = r.accounting.total_granted_bytes > 0 ?
            static_cast<double>(r.accounting.total_granted_bytes - r.accounting.total_requested_bytes) /
            static_cast<double>(r.accounting.total_granted_bytes) : 0.0;
        e.external_fragmentation = r.fragmentation.external_fragmentation;
        e.reuse_rate = r.reuse.reuse_rate();
        e.retained_bytes = r.reuse.backing_retained_after_workload;
        e.trim_recovery = r.reuse.trim_recovery_bytes;
        report.entries.push_back(e);
    }
    return report;
}

void apply_weighted_score(ComparisonReport& report) {
    if (report.weights.empty()) { report.has_weighted_score = false; return; }
    report.has_weighted_score = true;
    auto dim = [&](const char* name) -> double {
        for (const auto& w : report.weights) if (w.name == name) return w.weight;
        return 0.0;
    };
    auto norm = [](double v) { return std::isnan(v) || std::isinf(v) ? 0.0 : v; };
    for (auto& e : report.entries) {
        double score = 0.0;
        double w = norm(dim("throughput")); if (w > 0) score += w * e.ops_per_sec;
        w = norm(dim("p99"));        if (w > 0) score += w * (e.alloc_p99 > 0 ? 1.0 / e.alloc_p99 : 0.0);
        w = norm(dim("latency"));    if (w > 0) score += w * (e.alloc_mean_ns > 0 ? 1.0 / e.alloc_mean_ns : 0.0);
        w = norm(dim("memory_overhead")); if (w > 0) score += w * (e.peak_reserved > 0 ? 1.0 / e.peak_reserved : 0.0);
        w = norm(dim("fragmentation")); if (w > 0) score += w * (1.0 - std::min(1.0, e.external_fragmentation));
        w = norm(dim("failure_resilience")); if (w > 0) score += w * e.success_rate;
        e.weighted_score = score;
    }
    // Normalize scores to [0,1] by best in set (higher better).
    double best = 0.0;
    for (const auto& e : report.entries) best = std::max(best, e.weighted_score);
    if (best > 0) for (auto& e : report.entries) e.weighted_score /= best;
}

std::string report_to_text(const ComparisonReport& report) {
    std::ostringstream os;
    os << "Comparison Report\n";
    if (!report.workload_desc.empty()) os << "Workload: " << report.workload_desc << "\n";
    os << std::left << std::setw(16) << "allocator"
       << std::setw(12) << "ops/s"
       << std::setw(10) << "mean_ns"
       << std::setw(9)  << "p95"
       << std::setw(9)  << "p99"
       << std::setw(10) << "free_ns"
       << std::setw(7)  << "succ%"
       << std::setw(12) << "pk_resv"
       << std::setw(8)  << "waste%"
       << std::setw(7)  << "extFrag";
    if (report.has_weighted_score) os << std::setw(10) << "score";
    os << "\n";
    for (const auto& e : report.entries) {
        os << std::left << std::setw(16) << e.name
           << std::setw(12) << std::fixed << std::setprecision(1) << e.ops_per_sec
           << std::setw(10) << std::setprecision(1) << e.alloc_mean_ns
           << std::setw(9)  << e.alloc_p95
           << std::setw(9)  << e.alloc_p99
           << std::setw(10) << e.free_mean_ns
           << std::setw(7)  << std::setprecision(0) << (e.success_rate * 100.0)
           << std::setw(12) << e.peak_reserved
           << std::setw(8)  << std::setprecision(1) << (e.internal_waste * 100.0)
           << std::setw(7)  << std::setprecision(3) << e.external_fragmentation;
        if (report.has_weighted_score) os << std::setw(10) << std::setprecision(3) << e.weighted_score;
        os << "\n";
    }
    if (report.has_weighted_score) os << "\nWeighted scores are caller-configured; higher is better. Not a universal ranking.\n";
    return os.str();
}

std::string report_to_json(const ComparisonReport& report) {
    std::ostringstream os;
    os << "{\n  \"workload\": \"" << report.workload_desc << "\",\n";
    os << "  \"weighted_score\": " << (report.has_weighted_score ? "true" : "false") << ",\n";
    os << "  \"entries\": [\n";
    for (std::size_t i = 0; i < report.entries.size(); ++i) {
        const auto& e = report.entries[i];
        os << "    { \"allocator\": \"" << e.name << "\", \"ops_per_sec\": " << e.ops_per_sec
           << ", \"alloc_mean_ns\": " << e.alloc_mean_ns << ", \"alloc_p95\": " << e.alloc_p95
           << ", \"alloc_p99\": " << e.alloc_p99 << ", \"free_mean_ns\": " << e.free_mean_ns
           << ", \"realloc_mean_ns\": " << e.realloc_mean_ns
           << ", \"success\": " << e.success << ", \"failure\": " << e.failure
           << ", \"success_rate\": " << e.success_rate
           << ", \"peak_live_bytes\": " << e.peak_live_bytes
           << ", \"peak_reserved\": " << e.peak_reserved
           << ", \"internal_waste\": " << e.internal_waste
           << ", \"external_fragmentation\": " << e.external_fragmentation
           << ", \"reuse_rate\": " << e.reuse_rate
           << ", \"retained_bytes\": " << e.retained_bytes
           << ", \"growth_events\": " << e.growth_events
           << ", \"trim_recovery\": " << e.trim_recovery << " }";
        if (i + 1 < report.entries.size()) os << ",";
        os << "\n";
    }
    os << "  ]\n}\n";
    return os.str();
}

std::string report_to_csv(const ComparisonReport& report) {
    std::ostringstream os;
    os << "allocator,kind,ops_per_sec,alloc_mean_ns,alloc_p95,alloc_p99,free_mean_ns,realloc_mean_ns,success_rate,peak_live_bytes,peak_reserved,internal_waste,external_fragmentation,reuse_rate,retained_bytes,trim_recovery\n";
    for (const auto& e : report.entries) {
        os << e.name << "," << static_cast<int>(e.kind) << "," << e.ops_per_sec
           << "," << e.alloc_mean_ns << "," << e.alloc_p95 << "," << e.alloc_p99
           << "," << e.free_mean_ns << "," << e.realloc_mean_ns << "," << e.success_rate
           << "," << e.peak_live_bytes << "," << e.peak_reserved << "," << e.internal_waste
           << "," << e.external_fragmentation << "," << e.reuse_rate << "," << e.retained_bytes
           << "," << e.trim_recovery << "\n";
    }
    return os.str();
}

std::string result_to_text(const ExperimentResult& r) {
    std::ostringstream os;
    os << "Allocator: " << r.allocator_name << " (id " << r.allocator_id << ")\n";
    os << "  ops=" << r.operations << " success=" << r.success_count << " failure=" << r.failure_count << " seed=" << r.seed << "\n";
    os << "  alloc latency ns: mean=" << r.alloc_latency.mean << " p50=" << r.alloc_latency.p50 << " p90=" << r.alloc_latency.p90 << " p95=" << r.alloc_latency.p95 << " p99=" << r.alloc_latency.p99 << " max=" << r.alloc_latency.max << "\n";
    os << "  free  latency ns: mean=" << r.free_latency.mean << " p95=" << r.free_latency.p95 << " p99=" << r.free_latency.p99 << "\n";
    os << "  realloc latency ns: mean=" << r.realloc_latency.mean << " p95=" << r.realloc_latency.p95 << " p99=" << r.realloc_latency.p99 << "\n";
    os << "  throughput: " << r.throughput.ops_per_sec() << " ops/s, " << r.throughput.allocs_per_sec() << " alloc/s\n";
    os << "  live: count=" << r.live.live_count << " bytes=" << r.live.live_bytes << " peak=" << r.live.peak_live_bytes << " reserved=" << r.live.reserved << "\n";
    os << "  internal_waste=" << r.accounting.internal_waste << " external_frag=" << r.fragmentation.external_fragmentation << "\n";
    os << "  reuse_rate=" << r.reuse.reuse_rate() << " retained=" << r.reuse.backing_retained_after_workload << "\n";
    os << "  elapsed=" << r.elapsed_ns << " ns\n";
    return os.str();
}

std::string result_to_json(const ExperimentResult& r) {
    std::ostringstream os;
    os << "{ \"allocator\": \"" << r.allocator_name << "\", \"ops\": " << r.operations
       << ", \"success\": " << r.success_count << ", \"failure\": " << r.failure_count
       << ", \"alloc_mean_ns\": " << r.alloc_latency.mean << ", \"alloc_p99\": " << r.alloc_latency.p99
       << ", \"free_mean_ns\": " << r.free_latency.mean << ", \"realloc_mean_ns\": " << r.realloc_latency.mean
       << ", \"ops_per_sec\": " << r.throughput.ops_per_sec()
       << ", \"peak_live_bytes\": " << r.live.peak_live_bytes << ", \"peak_reserved\": " << r.live.peak_reserved
       << ", \"internal_waste\": " << r.accounting.internal_waste
       << ", \"external_frag\": " << r.fragmentation.external_fragmentation
       << ", \"reuse_rate\": " << r.reuse.reuse_rate() << " }\n";
    return os.str();
}

std::string result_to_csv_header() {
    return "allocator,operations,success,failure,ops_per_sec,alloc_mean_ns,alloc_p95,alloc_p99,free_mean_ns,realloc_mean_ns,peak_live_bytes,peak_reserved,internal_waste,external_frag,reuse_rate,retained_bytes,elapsed_ns\n";
}

std::string result_to_csv_row(const ExperimentResult& r) {
    std::ostringstream os;
    os << r.allocator_name << "," << r.operations << "," << r.success_count << "," << r.failure_count
       << "," << r.throughput.ops_per_sec() << "," << r.alloc_latency.mean << "," << r.alloc_latency.p95
       << "," << r.alloc_latency.p99 << "," << r.free_latency.mean << "," << r.realloc_latency.mean
       << "," << r.live.peak_live_bytes << "," << r.live.peak_reserved << "," << r.accounting.internal_waste
       << "," << r.fragmentation.external_fragmentation << "," << r.reuse.reuse_rate()
       << "," << r.reuse.backing_retained_after_workload << "," << r.elapsed_ns << "\n";
    return os.str();
}

std::string explain_comparison(const std::vector<ExperimentResult>& results, const std::string& workload_desc) {
    std::ostringstream os;
    os << "Explanation (measured facts + heuristic interpretation)\n";
    os << "Workload: " << workload_desc << "\n\n";
    if (results.empty()) { os << "No results.\n"; return os.str(); }
    const ExperimentResult* best_throughput = nullptr;
    const ExperimentResult* best_p99 = nullptr;
    const ExperimentResult* lowest_mem = nullptr;
    for (const auto& r : results) {
        if (!best_throughput || r.throughput.ops_per_sec() > best_throughput->throughput.ops_per_sec()) best_throughput = &r;
        if (!best_p99 || r.alloc_latency.p99 < best_p99->alloc_latency.p99) best_p99 = &r;
        if (!lowest_mem || r.live.peak_reserved < lowest_mem->live.peak_reserved) lowest_mem = &r;
    }
    os << "Measured facts (correlation, not causal claims):\n";
    os << "  highest throughput: " << best_throughput->allocator_name << " (" << best_throughput->throughput.ops_per_sec() << " ops/s)\n";
    os << "  best p99 alloc latency: " << best_p99->allocator_name << " (" << best_p99->alloc_latency.p99 << " ns)\n";
    os << "  lowest peak reserved: " << lowest_mem->allocator_name << " (" << lowest_mem->live.peak_reserved << " bytes)\n\n";
    os << "Heuristic interpretation (not universal, workload-dependent):\n";
    for (const auto& r : results) {
        os << "  - " << r.allocator_name << ": ";
        if (r.accounting.internal_waste > 0) os << "rounds sizes up (internal waste " << r.accounting.internal_waste << " B); ";
        if (r.reuse.backing_retained_after_workload > r.live.peak_live_bytes) os << "retains backing after workload; ";
        if (r.capabilities.allows_per_object_free == false) os << "no per-object free (arena-like semantics); ";
        if (r.fragmentation.external_fragmentation > 0.5) os << "high external fragmentation; ";
        if (!r.capabilities.supports_free) os << "free unsupported; ";
        os << "\n";
    }
    os << "\nSemantic caveats: arena/bump allocators do not free individual objects; pools retain backing; CUDA/pinned backends have host-side setup cost. Compare only within the same memory domain and policy set.\n";
    return os.str();
}

Error write_text_file(const std::filesystem::path& path, const std::string& content) {
    std::ofstream f(path, std::ios::binary);
    if (!f) return make_error(ErrorCode::operation_failed, "cannot open output file: " + path.string());
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return f ? Error{} : make_error(ErrorCode::operation_failed, "cannot write output file");
}

} // namespace allocator_lab
