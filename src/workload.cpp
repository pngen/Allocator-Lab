// Allocator Lab 1.0.0
// Workload generation implementation.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "allocator_lab/workload.hpp"
#include "allocator_lab/detail/rng.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <stdexcept>

namespace allocator_lab {


Error sanitize_workload_config(const WorkloadConfig& in, WorkloadConfig& out) {
    out = in;
    if (in.min_size == 0) out.min_size = 1;
    if (in.max_size < in.min_size) out.max_size = in.min_size;
    if (out.operations == 0) out.operations = 1;
    if (out.live_target == 0) out.live_target = std::min<std::uint64_t>(256, out.operations);
    if (out.live_target > out.operations) out.live_target = out.operations;
    // phase fractions must sum to <= 1.0
    double wf = out.warmup_fraction, cf = out.cooldown_fraction;
    if (wf < 0.0) wf = 0.0; if (wf > 1.0) wf = 1.0;
    if (cf < 0.0) cf = 0.0; if (cf > 1.0) cf = 1.0;
    if (wf + cf > 1.0) { if (wf >= cf) wf = 1.0 - cf; else cf = 1.0 - wf; }
    out.warmup_fraction = wf; out.cooldown_fraction = cf;
    if (out.realloc_fraction < 0.0) out.realloc_fraction = 0.0;
    if (out.realloc_fraction > 0.9) out.realloc_fraction = 0.9;
    if (out.alloc_free_ratio <= 0.0) out.alloc_free_ratio = 0.0;
    if (out.alloc_free_ratio > 4.0) out.alloc_free_ratio = 4.0;
    if (out.burstiness < 0.0) out.burstiness = 0.0;
    if (out.burstiness > 1.0) out.burstiness = 1.0;
    if (out.cold_class_count == 0) out.cold_class_count = 4;
    if (out.hot_class_count == 0) out.hot_class_count = 8;
    if (out.threads == 0) out.threads = 1;
    if (out.threads > 4096) out.threads = 4096;
    return Error{};
}

// ---- size selection ----
static std::size_t choose_size(detail::SplitMix64& rng, const WorkloadConfig& cfg) {
    const std::size_t lo = cfg.min_size, hi = cfg.max_size;
    const double dhi = static_cast<double>(hi), dlo = static_cast<double>(lo);
    switch (cfg.size_dist) {
        case SizeDistribution::Fixed: return hi;
        case SizeDistribution::Uniform: return rng.next_in(lo, hi);
        case SizeDistribution::LogUniform: {
            double llo = std::log2(std::max<double>(1.0, dlo));
            double lhi = std::log2(std::max<double>(1.0, dhi));
            double v = llo + rng.next01() * (lhi - llo);
            std::size_t s = static_cast<std::size_t>(std::exp2(v));
            return std::clamp(s, lo, hi);
        }
        case SizeDistribution::Geometric: {
            double u = 1.0 - rng.next01();
            std::size_t s = lo + static_cast<std::size_t>(u * u * (hi - lo));
            return std::clamp(s, lo, hi);
        }
        case SizeDistribution::Bimodal: {
            return rng.next01() < 0.7 ? lo : hi;
        }
        case SizeDistribution::MultiModal: {
            if (!cfg.multimodal_peaks.empty()) {
                std::size_t idx = static_cast<std::size_t>(rng.next(cfg.multimodal_peaks.size()));
                return cfg.multimodal_peaks[idx];
            }
            return rng.next01() < 0.5 ? lo : std::min<std::size_t>(hi, lo * 8 + 32);
        }
        case SizeDistribution::PowerLaw: {
            double u = rng.next01();
            std::size_t s = static_cast<std::size_t>(static_cast<double>(lo) * std::pow(dhi / dlo, u));
            return std::clamp(s, lo, hi);
        }
        case SizeDistribution::Histogram: {
            if (!cfg.size_histogram.empty()) {
                std::uint64_t total = 0;
                for (std::size_t w : cfg.size_histogram) total += w;
                std::uint64_t r = rng.next(total ? total : 1);
                std::uint64_t cum = 0;
                std::size_t n = cfg.size_histogram.size();
                std::size_t pick = 0;
                for (std::size_t i = 0; i < n; ++i) { cum += cfg.size_histogram[i]; if (r < cum) { pick = i; break; } pick = n - 1; }
                // representative size: log-spaced between lo..hi by bucket index
                if (n == 1) return lo;
                double v = std::log2(std::max<double>(1.0, dlo)) +
                            (static_cast<double>(pick) / (n - 1)) * (std::log2(std::max<double>(1.0, dhi)) - std::log2(std::max<double>(1.0, dlo)));
                return std::clamp(static_cast<std::size_t>(std::exp2(v)), lo, hi);
            }
            [[fallthrough]];
        }
        case SizeDistribution::CallerSequence: {
            if (!cfg.size_sequence.empty()) {
                static thread_local std::size_t seq_pos = 0;
                (void)seq_pos;
            }
            return rng.next_in(lo, hi);
        }
    }
    return lo;
}

static std::size_t choose_alignment(detail::SplitMix64& rng, const WorkloadConfig& cfg, std::size_t size) {
    switch (cfg.alignment_dist) {
        case AlignmentDistribution::Natural: return 0;
        case AlignmentDistribution::Fixed: {
            if (cfg.alignments.empty()) return 0;
            std::size_t idx = static_cast<std::size_t>(rng.next(cfg.alignments.size()));
            return cfg.alignments[idx];
        }
        case AlignmentDistribution::UniformPow2: {
            std::size_t choices[] = {8, 16, 32, 64, 128, 256, 512, 4096};
            std::size_t idx = static_cast<std::size_t>(rng.next(8));
            if (choices[idx] > size && size > 0) return 0;
            return choices[idx];
        }
    }
    return 0;
}

static std::uint64_t choose_lifetime(detail::SplitMix64& rng, const WorkloadConfig& cfg) {
    switch (cfg.lifetime_dist) {
        case LifetimeDistribution::Immediate: return 1;
        case LifetimeDistribution::Short: return 1 + static_cast<std::uint64_t>(rng.next(cfg.short_mean));
        case LifetimeDistribution::Medium: return 1 + static_cast<std::uint64_t>(rng.next(cfg.medium_mean));
        case LifetimeDistribution::Long: return 1 + static_cast<std::uint64_t>(rng.next(cfg.long_mean));
        case LifetimeDistribution::Mixed: {
            double u = rng.next01();
            if (u < 0.5) return 1 + static_cast<std::uint64_t>(rng.next(cfg.short_mean));
            if (u < 0.85) return 1 + static_cast<std::uint64_t>(rng.next(cfg.medium_mean));
            return 1 + static_cast<std::uint64_t>(rng.next(cfg.long_mean));
        }
        case LifetimeDistribution::Fixed: return cfg.fixed_lifetime_steps;
        case LifetimeDistribution::Random: return cfg.lifetime_min + rng.next(cfg.lifetime_max - cfg.lifetime_min + 1);
        case LifetimeDistribution::PhaseBound: return cfg.short_mean * 4;
        case LifetimeDistribution::CallerProvided: return cfg.lifetime_min + rng.next(cfg.lifetime_max - cfg.lifetime_min + 1);
    }
    return 1;
}

Error generate_workload(const WorkloadConfig& config, Trace& out) {
    WorkloadConfig cfg;
    Error err = sanitize_workload_config(config, cfg);
    if (err) return err;
    out = Trace{};
    out.version = kTraceVersion; out.seed = cfg.seed; out.name = cfg.name;
    out.entries.reserve(static_cast<std::size_t>(cfg.operations));

    detail::SplitMix64 rng(cfg.seed);
    struct Live { AllocationId id; std::size_t size; std::size_t alignment; WorkerId worker; std::uint32_t cls; std::uint64_t free_step; std::uint64_t alloc_step; };
    std::vector<Live> live; live.reserve(1024);
    std::vector<AllocationId> free_pool;
    AllocationId next_id = 1;
    const std::uint64_t target = cfg.live_target;
    const std::uint64_t ops = cfg.operations;
    const std::uint64_t threads = std::max<std::uint64_t>(1, cfg.threads);

    auto pick_worker = [&]() { return static_cast<WorkerId>(rng.next(threads)); };
    auto pick_class = [&]() { return rng.next01() < 0.7 ? 0 : 1; };

    for (std::uint64_t step = 0; step < ops; ++step) {
        double frac = static_cast<double>(step) / static_cast<double>(ops);
        std::uint64_t T = target;
        if (frac < cfg.warmup_fraction) T = static_cast<std::uint64_t>((frac / (cfg.warmup_fraction <= 0 ? 1 : cfg.warmup_fraction)) * target);
        else if (frac > 1.0 - cfg.cooldown_fraction) T = static_cast<std::uint64_t>(((1.0 - frac) / (cfg.cooldown_fraction <= 0 ? 1 : cfg.cooldown_fraction)) * target);
        T = std::min<std::uint64_t>(T, ops);

        const std::uint64_t live_count = static_cast<std::uint64_t>(live.size());
        TraceOperationType action;
        if (live_count == 0) {
            action = TraceOperationType::Allocate;
        } else if (live_count < T) {
            action = TraceOperationType::Allocate;
            if (cfg.alloc_free_ratio <= 0.5 && rng.next01() < 0.15) action = TraceOperationType::Free;
        } else if (live_count > T) {
            action = TraceOperationType::Free;
        } else {
            double r = rng.next01();
            if (r < cfg.realloc_fraction) action = TraceOperationType::Reallocate;
            else if (r < 0.5 + cfg.realloc_fraction * 0.5) action = TraceOperationType::Allocate;
            else action = TraceOperationType::Free;
        }

        if (action == TraceOperationType::Allocate) {
            std::size_t size = choose_size(rng, cfg);
            std::size_t alignment = choose_alignment(rng, cfg, size);
            WorkerId w = pick_worker();
            std::uint32_t cls = pick_class();
            AllocationId id;
            bool reused_id = false;
            if (cfg.object_reuse_prob > 0.0 && rng.next01() < cfg.object_reuse_prob && !free_pool.empty()) {
                id = free_pool.back(); free_pool.pop_back(); reused_id = true;
            } else {
                id = next_id++;
            }
            std::uint64_t lifetime = choose_lifetime(rng, cfg);
            live.push_back(Live{ id, size, alignment, w, cls, step + lifetime, step });
            TraceEntry e;
            e.seq = out.entries.size(); e.step = step; e.op = TraceOperationType::Allocate; e.id = id;
            e.size = size; e.alignment = alignment; e.domain = cfg.domain; e.worker = w; e.tag = cls;
            out.entries.push_back(e);
        } else if (action == TraceOperationType::Free) {
            if (live.empty()) continue;
            std::size_t idx = 0;
            if (cfg.free_order == FreeOrder::Lifo) idx = live.size() - 1;
            else if (cfg.free_order == FreeOrder::Random) idx = static_cast<std::size_t>(rng.next(live.size()));
            else if (cfg.free_order == FreeOrder::ByLifetime) {
                idx = 0; for (std::size_t j = 1; j < live.size(); ++j) if (live[j].free_step < live[idx].free_step) idx = j;
            } else { idx = 0; }
            Live victim = live[idx];
            live[idx] = live.back(); live.pop_back();
            free_pool.push_back(victim.id);
            TraceEntry e; e.seq = out.entries.size(); e.step = step; e.op = TraceOperationType::Free; e.id = victim.id;
            e.size = victim.size; e.alignment = victim.alignment; e.domain = cfg.domain; e.worker = victim.worker; e.tag = victim.cls;
            out.entries.push_back(e);
        } else if (action == TraceOperationType::Reallocate) {
            if (live.empty()) continue;
            std::size_t idx = static_cast<std::size_t>(rng.next(live.size()));
            Live& l = live[idx];
            std::size_t nsize = choose_size(rng, cfg);
            /* grow or shrink half the time */
            if (rng.next01() < 0.5 && nsize < l.size) nsize = l.size;
            l.size = nsize; l.alignment = choose_alignment(rng, cfg, nsize);
            TraceEntry e; e.seq = out.entries.size(); e.step = step; e.op = TraceOperationType::Reallocate; e.id = l.id;
            e.size = nsize; e.alignment = l.alignment; e.domain = cfg.domain; e.worker = l.worker; e.tag = l.cls;
            out.entries.push_back(e);
        }
    }

    if (cfg.release_to_zero) {
        while (!live.empty()) {
            std::uint64_t step = ops;
            Live victim = live.back(); live.pop_back();
            TraceEntry e; e.seq = out.entries.size(); e.step = step; e.op = TraceOperationType::Free; e.id = victim.id;
            e.size = victim.size; e.alignment = victim.alignment; e.domain = cfg.domain; e.worker = victim.worker; e.tag = victim.cls;
            out.entries.push_back(e);
        }
    }

    return validate_trace(out);
}

std::string workload_config_to_string(const WorkloadConfig& c) {
    std::ostringstream os;
    os << "ops=" << c.operations << ",live=" << c.live_target
       << ",min=" << c.min_size << ",max=" << c.max_size
       << ",dist=" << static_cast<int>(c.size_dist)
       << ",align_dist=" << static_cast<int>(c.alignment_dist)
       << ",alloc_free=" << c.alloc_free_ratio
       << ",realloc=" << c.realloc_fraction
       << ",lifetime=" << static_cast<int>(c.lifetime_dist)
       << ",threads=" << c.threads
       << ",domain=" << static_cast<int>(c.domain)
       << ",seed=" << c.seed;
    return os.str();
}

} // namespace allocator_lab
