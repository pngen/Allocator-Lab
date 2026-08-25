// Example: file-backed / memory-mapped allocation experiment.
// Demonstrates create -> map -> write -> unmap -> reopen -> verify -> cleanup.
#include "allocator_lab/allocators/mapped_allocator.hpp"
#include <cstdio>
#include <cstring>
#include <iostream>
using namespace allocator_lab;
int main() {
    const char* path = "allocator_lab_example.map";
    // 1) Create + map.
    MappedAllocator m(path, 4u << 20, true);
    if (!m.initialized()) { std::cerr << "mapped allocator init failed\n"; return 1; }
    std::cout << "setup latency ns=" << m.setup_latency_ns() << "\n";
    // 2) Write data into the mapping (allocation + deterministic bytes).
    AllocationRequest req; req.size = 2048;
    AllocationResult r = m.allocate(req);
    if (!r.error.ok()) { std::cerr << "mapped alloc failed: " << r.error.message << "\n"; return 2; }
    std::uint8_t* p = static_cast<std::uint8_t*>(r.address);
    for (int i = 0; i < static_cast<int>(req.size); ++i) p[i] = static_cast<std::uint8_t>((i * 7) & 0xFF);
    std::cout << "mapped alloc ok size=" << r.size << "\n";
    // 3) Unmap (close the view/mapping). Data persists in the file.
    m.shutdown();
    // 4) Reopen the file-backed mapping and verify the data survived.
    MappedAllocator m2(path, 4u << 20, false);
    if (!m2.initialized()) { std::cerr << "mapped reopen failed\n"; return 3; }
    AllocationResult rv = m2.allocate(req);
    // A fresh bump starts at the header, so verify by reading the region instead:
    std::uint8_t* base = reinterpret_cast<std::uint8_t*>(m2.region());
    const std::size_t hdr = 16;  // MappedHeader{magic,next}
    bool ok = true;
    for (int i = 0; i < static_cast<int>(req.size); ++i) if (base[hdr + i] != static_cast<std::uint8_t>((i * 7) & 0xFF)) ok = false;
    std::cout << "reopen verify: " << (ok ? "data preserved" : "DATA MISMATCH") << "\n";
    // 5) Cleanup.
    m2.shutdown();
    std::remove(path);
    return ok ? 0 : 4;
}
