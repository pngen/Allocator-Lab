// Example: direct system allocation experiment.
#include "allocator_lab/allocators/system_allocator.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    SystemAllocator a;
    AllocationRequest req; req.size = 1024; req.alignment = 0;
    AllocationResult r = a.allocate(req);
    if (!r.error.ok()) { std::cerr << "allocate failed: " << r.error.message << "\n"; return 1; }
    std::cout << "system alloc: id=" << r.id << " size=" << r.size << " align=" << r.alignment << " addr=" << r.address << "\n";
    AllocationInfo info; a.query(r.id, info);
    a.free(r.id);
    std::cout << "live after free: " << a.statistics().live.live_count << "\n";
    return 0;
}
