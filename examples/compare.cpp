// Example: compare direct vs pool allocator on the same workload.
#include "allocator_lab/allocators.hpp"
#include "allocator_lab/experiment.hpp"
#include "allocator_lab/report.hpp"
#include "allocator_lab/provenance.hpp"
#include "allocator_lab/workload.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    WorkloadConfig wc; wc.operations = 50000; wc.live_target = 1024; wc.seed = 7; wc.size_dist = SizeDistribution::LogUniform;
    Trace trace; generate_workload(wc, trace);
    std::vector<ExperimentResult> res;
    for (const char* n : { "system", "fixed_pool" }) {
        std::unique_ptr<AllocatorStrategy> a;
        if (n[0]=='s') a = std::make_unique<SystemAllocator>(); else a = std::make_unique<FixedPool>(128, 1u<<20);
        ExperimentConfig ec; ec.verify_payload = true;
        res.push_back(run_replay(*a, trace, ec, make_provenance("example")));
    }
    ComparisonReport rep = build_comparison(res); rep.workload_desc = "compare example";
    std::cout << report_to_text(rep);
    return 0;
}
