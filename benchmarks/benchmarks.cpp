// Allocator Lab 1.0.0 benchmark suite.
#include "allocator_lab/allocators.hpp"
#include "allocator_lab/experiment.hpp"
#include "allocator_lab/report.hpp"
#include "allocator_lab/provenance.hpp"
#include "allocator_lab/workload.hpp"
#include <iostream>
#include <string>
#include <vector>

using namespace allocator_lab;

static std::unique_ptr<AllocatorStrategy> make_all(const std::string& name) {
    if (name == "system") return std::make_unique<SystemAllocator>();
    if (name == "aligned") return std::make_unique<AlignedAllocator>();
    if (name == "fixed_pool") return std::make_unique<FixedPool>(64, 1u << 22);
    if (name == "size_class_pool") return std::make_unique<SizeClassPool>();
    if (name == "slab") return std::make_unique<SlabAllocator>(256);
    if (name == "arena") return std::make_unique<ArenaAllocator>();
    if (name == "free_list") return std::make_unique<FreeListAllocator>();
    if (name == "buddy") return std::make_unique<BuddyAllocator>(23);
    if (name == "segregated_fit") return std::make_unique<SegregatedAllocator>();
    return nullptr;
}

static WorkloadConfig wc(const char* name, std::uint64_t ops, std::uint64_t live, SizeDistribution dist) {
    WorkloadConfig c; c.name = name; c.operations = ops; c.live_target = live; c.seed = 12345;
    c.size_dist = dist; c.min_size = 16; c.max_size = 4096;
    return c;
}

int main() {
    std::cout << "Allocator Lab benchmark suite\n";
    const std::vector<std::string> allocs = { "system", "aligned", "fixed_pool", "size_class_pool", "arena", "free_list", "buddy", "segregated_fit" };
    struct W { const char* name; WorkloadConfig cfg; };
    std::vector<W> workloads = {
        { "tiny_fixed", wc("tiny_fixed", 200000, 512, SizeDistribution::Fixed) },
        { "uniform_mixed", wc("uniform_mixed", 200000, 2048, SizeDistribution::Uniform) },
        { "log_uniform_mixed", wc("log_uniform_mixed", 200000, 2048, SizeDistribution::LogUniform) },
        { "bimodal", wc("bimodal", 200000, 2048, SizeDistribution::Bimodal) },
    };
    // tiny_fixed uses a genuinely tiny fixed object (64 B), which fixed_pool(64) serves.
    workloads[0].cfg.min_size = 64; workloads[0].cfg.max_size = 64;
    std::cout << "allocator,workload,ops_per_sec,mean_ns,p99_ns,peak_live\n";
    for (const auto& wl : workloads) {
        for (const auto& an : allocs) {
            auto a = make_all(an); if (!a) continue;
            Trace trace; if (generate_workload(wl.cfg, trace)) continue;
            ExperimentConfig ec; ec.verify_payload = false;
            ExperimentResult r = run_replay(*a, trace, ec, make_provenance("benchmark"));
            std::cout << an << "," << wl.name << "," << r.throughput.ops_per_sec() << ","
                      << r.alloc_latency.mean << "," << r.alloc_latency.p99 << "," << r.live.peak_live_bytes << "\n";
            a->shutdown();
        }
    }
    return 0;
}
