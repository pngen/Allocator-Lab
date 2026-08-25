// Example: replay a generated trace against multiple allocators deterministically.
#include "allocator_lab/allocators.hpp"
#include "allocator_lab/experiment.hpp"
#include "allocator_lab/report.hpp"
#include "allocator_lab/provenance.hpp"
#include "allocator_lab/workload.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    WorkloadConfig wc; wc.operations = 5000; wc.live_target = 128; wc.seed = 9; wc.size_dist = SizeDistribution::Bimodal; wc.min_size = 16; wc.max_size = 4096;
    Trace trace; generate_workload(wc, trace);
    std::vector<ExperimentResult> results;
    for (const char* n : { "system", "aligned", "free_list", "buddy" }) {
        std::unique_ptr<AllocatorStrategy> a;
        if (std::string(n) == "system") a = std::make_unique<SystemAllocator>();
        else if (std::string(n) == "aligned") a = std::make_unique<AlignedAllocator>();
        else if (std::string(n) == "free_list") a = std::make_unique<FreeListAllocator>();
        else a = std::make_unique<BuddyAllocator>(22);
        ExperimentConfig ec; ec.verify_payload = false;
        results.push_back(run_replay(*a, trace, ec, make_provenance("example")));
        a->shutdown();
    }
    auto rep = build_comparison(results); rep.workload_desc = "replay example";
    std::cout << report_to_text(rep);
    return 0;
}
