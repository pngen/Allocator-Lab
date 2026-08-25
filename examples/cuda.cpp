// Example: real CUDA device allocation experiment.
#include "allocator_lab/allocators/cuda_allocator.hpp"
#include "allocator_lab/backend.hpp"
#include <iostream>
using namespace allocator_lab;
int main() {
    DeviceInfo di = query_device_info("cuda");
    if (!di.available) { std::cout << "CUDA device not available\n"; return 0; }
    std::cout << "device: " << di.name << " total=" << (di.total_memory_bytes >> 20) << " MiB free=" << (di.free_memory_bytes >> 20) << " MiB\n";
    CudaDirectAllocator a;
    for (int i = 0; i < 4; ++i) {
        AllocationRequest req; req.size = 16u << 20; req.domain = MemoryDomain::Device;
        AllocationResult r = a.allocate(req);
        if (r.error.ok()) { std::cout << "alloc[" << i << "] ok size=" << r.size << "\n"; a.free(r.id); }
        else std::cout << "alloc[" << i << "] failed: " << r.error.message << "\n";
    }
    a.shutdown();
    DeviceInfo after = query_device_info("cuda");
    std::cout << "free after cleanup=" << (after.free_memory_bytes >> 20) << " MiB\n";
    return 0;
}
