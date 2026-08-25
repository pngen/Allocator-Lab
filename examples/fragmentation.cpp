// Example: fragmentation measurement under checkerboard churn.
#include "allocator_lab/allocators.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    FreeListAllocator fl;
    std::vector<AllocationId> keep;
    // Interleave long-lived sentinels with churn.
    for (int i = 0; i < 100; ++i) {
        AllocationRequest req; req.size = 128;
        AllocationResult r = fl.allocate(req); if (r.error.ok()) keep.push_back(r.id);
    }
    for (int cycle = 0; cycle < 200; ++cycle) {
        std::vector<AllocationId> tmp;
        for (int i = 0; i < 40; ++i) { AllocationRequest req; req.size = 64 + (i % 8) * 64; AllocationResult r = fl.allocate(req); if (r.error.ok()) tmp.push_back(r.id); }
        for (auto id : tmp) fl.free(id);
    }
    auto st = fl.statistics();
    std::cout << "external fragmentation: " << st.fragmentation.external_fragmentation << " largest_free=" << st.fragmentation.largest_free_span
              << " free_spans=" << st.fragmentation.free_span_count << "\n";
    for (auto id : keep) fl.free(id);
    return 0;
}
