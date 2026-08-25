// Example: deterministic trace generation, serialization, deserialization.
#include "allocator_lab/trace.hpp"
#include "allocator_lab/workload.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    WorkloadConfig wc; wc.operations = 2000; wc.live_target = 128; wc.seed = 42; wc.size_dist = SizeDistribution::LogUniform;
    Trace t; generate_workload(wc, t);
    std::string json = trace_to_json(t);
    Trace back; if (trace_from_json(json, back)) { std::cerr << "deserialize failed\n"; return 1; }
    std::cout << "roundtrip ok: entries=" << back.entry_count() << " json_bytes=" << json.size() << "\n";
    std::vector<std::uint8_t> bin = trace_to_binary(t);
    Trace back2; if (trace_from_binary(bin, back2)) { std::cerr << "binary deserialize failed\n"; return 2; }
    std::cout << "binary roundtrip ok: entries=" << back2.entry_count() << " bytes=" << bin.size() << "\n";
    return 0;
}
