// Example: generate a JSON/CSV report from an experiment.
#include "allocator_lab/allocators.hpp"
#include "allocator_lab/experiment.hpp"
#include "allocator_lab/report.hpp"
#include "allocator_lab/provenance.hpp"
#include "allocator_lab/workload.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    WorkloadConfig wc; wc.operations = 20000; wc.live_target = 256; wc.seed = 5; wc.size_dist = SizeDistribution::Bimodal;
    Trace trace; generate_workload(wc, trace);
    SystemAllocator a;
    ExperimentConfig ec; ec.verify_payload = true;
    ExperimentResult r = run_replay(a, trace, ec, make_provenance("example"));
    std::cout << result_to_json(r);
    std::cout << "\nCSV:\n" << result_to_csv_header() << result_to_csv_row(r);
    return 0;
}
