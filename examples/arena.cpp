// Example: arena (bump) workload, no per-object free.
#include "allocator_lab/allocators.hpp"
#include "allocator_lab/experiment.hpp"
#include "allocator_lab/report.hpp"
#include "allocator_lab/provenance.hpp"
#include "allocator_lab/workload.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    WorkloadConfig wc; wc.operations = 20000; wc.live_target = 1024; wc.seed = 11; wc.size_dist = SizeDistribution::Uniform;
    Trace trace; generate_workload(wc, trace);
    ArenaAllocator a;
    ExperimentConfig ec; ec.verify_payload = false;
    ExperimentResult r = run_replay(a, trace, ec, make_provenance("example"));
    std::cout << result_to_text(r);
    return 0;
}
