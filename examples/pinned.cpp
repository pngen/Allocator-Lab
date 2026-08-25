// Example: pinned host memory allocation experiment (succeeds only if CUDA present).
#include "allocator_lab/allocators/pinned_allocator.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    PinnedAllocator a(64ull << 20);
    if (!a.capabilities().supports_allocate) { std::cout << "pinned host memory not available in this build\n"; return 0; }
    AllocationRequest req; req.size = 64u << 20; req.domain = MemoryDomain::Pinned;
    AllocationResult r = a.allocate(req);
    if (r.error.ok()) { std::cout << "pinned alloc ok size=" << r.size << "\n"; a.free(r.id); }
    else std::cout << "pinned alloc failed: " << r.error.message << "\n";
    a.shutdown();
    return 0;
}
