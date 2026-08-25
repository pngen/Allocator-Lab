// Example: concurrent allocator comparison (thread scaling).
#include "allocator_lab/allocators.hpp"
#include "allocator_lab/experiment.hpp"
#include "allocator_lab/provenance.hpp"
#include "allocator_lab/workload.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    WorkloadConfig wc; wc.operations = 40000; wc.live_target = 2048; wc.threads = 8; wc.seed = 3; wc.size_dist = SizeDistribution::LogUniform; wc.max_size = 65536;
    Trace trace; generate_workload(wc, trace);
    SystemAllocator a;
    for (std::uint32_t t : { (std::uint32_t)1, (std::uint32_t)2, (std::uint32_t)4, (std::uint32_t)8 }) {
        ExperimentConfig ec; ec.verify_payload = false;
        ExperimentResult r = run_concurrent(a, trace, ec, t, make_provenance("example"));
        std::cout << "threads=" << t << " ops/s=" << r.throughput.ops_per_sec() << " p99=" << r.alloc_latency.p99 << "\n";
    }
    a.shutdown();
    return 0;
}
