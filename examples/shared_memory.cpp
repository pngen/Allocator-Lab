// Example: shared-memory allocator (single-process create/open demonstration).
#include "allocator_lab/allocators/shared_allocator.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    SharedAllocator a("allocator_lab_example_shm", 4u << 20, SharedAllocator::Mode::Create);
    if (!a.initialized()) { std::cerr << "shared allocator init failed\n"; return 1; }
    AllocationRequest req; req.size = 1024;
    AllocationResult r = a.allocate(req);
    if (r.error.ok()) {
        std::cout << "shared alloc ok size=" << r.size << "\n";
        a.publish(r.id);
        std::cout << "published offset=" << a.published_offset() << " size=" << a.published_size() << "\n";
    } else std::cout << "shared alloc failed: " << r.error.message << "\n";
    a.shutdown();
    return 0;
}
