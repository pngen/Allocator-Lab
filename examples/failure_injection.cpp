// Example: graceful capacity exhaustion.
#include "allocator_lab/allocators.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    FixedPool pool(64, 100);
    std::vector<AllocationId> ids;
    AllocationRequest req; req.size = 64;
    for (int i = 0; i < 200; ++i) {
        AllocationResult r = pool.allocate(req);
        if (r.error.ok()) ids.push_back(r.id);
        else { std::cout << "allocation " << i << " failed gracefully: " << r.error.code_name() << "\n"; break; }
    }
    std::cout << "live=" << pool.statistics().live.live_count << "\n";
    for (auto id : ids) pool.free(id);
    std::cout << "live after free=" << pool.statistics().live.live_count << "\n";
    return 0;
}
