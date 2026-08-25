// Example: size-class allocator workload with per-class behavior.
#include "allocator_lab/allocators.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    SizeClassPool pool;
    std::vector<AllocationId> ids;
    for (int i = 0; i < 5000; ++i) {
        AllocationRequest req; req.size = 1 + static_cast<std::size_t>(i % 2048);
        AllocationResult r = pool.allocate(req);
        if (r.error.ok()) ids.push_back(r.id);
    }
    std::cout << "allocated " << ids.size() << " objects\n";
    for (auto id : ids) pool.free(id);
    auto st = pool.statistics();
    std::cout << "live after free: " << st.live.live_count << " waste=" << st.accounting.internal_waste << " reuse=" << st.reuse.reuse_rate() << "\n";
    return 0;
}
