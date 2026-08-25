// Allocator Lab 1.0.0
// Shared-memory proof: the "creator" process. It creates a named shared mapping,
// allocates, writes a deterministic pattern, publishes the block, and stays alive
// until the verifier signals release, so the mapping stays open for a real second
// process to open and read. Cleanup happens on release.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "allocator_lab/allocators/shared_allocator.hpp"
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <chrono>
#include <filesystem>

using namespace allocator_lab;

static void write_pattern(void* p, std::size_t size, std::uint64_t seed) {
    // deterministic non-zero pattern
    std::uint8_t* b = static_cast<std::uint8_t*>(p);
    std::uint64_t s = seed ^ 0x9E3779B97F4A7C15ULL;
    for (std::size_t i = 0; i < size; ++i) { s = s * 6364136223846793005ULL + 1442695040888963407ULL; b[i] = static_cast<std::uint8_t>(s >> 33); }
}

int main(int argc, char** argv) {
    if (argc < 4) { std::fprintf(stderr, "usage: shm-creator <name> <size> <seed>\n"); return 2; }
    std::string name = argv[1];
    std::size_t size = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    std::uint64_t seed = std::strtoull(argv[3], nullptr, 10);

    const std::string ready = name + "_ready.txt";
    const std::string release = name + "_release.txt";
    const std::string meta = name + "_meta.txt";

    SharedAllocator s(name, size, SharedAllocator::Mode::Create);
    if (!s.initialized()) { std::fprintf(stderr, "creator: shared allocator init failed\n"); return 1; }

    AllocationRequest req; req.size = 1024;
    AllocationResult r = s.allocate(req);
    if (!r.error.ok()) { std::fprintf(stderr, "creator: allocate failed: %s\n", r.error.message.c_str()); return 1; }
    write_pattern(r.address, 1024, seed);
    if (!s.publish(r.id).ok()) { std::fprintf(stderr, "creator: publish failed\n"); return 1; }

    // Write meta (offset/size/seed) so the verifier knows where to look.
    {
        std::ofstream f(meta, std::ios::trunc);
        f << s.published_offset() << " " << s.published_size() << " " << seed << "\n";
    }
    // Signal ready.
    { std::ofstream f(ready, std::ios::trunc); f << "1\n"; }
    std::fprintf(stdout, "created offset=%llu size=%llu\n",
                 static_cast<unsigned long long>(s.published_offset()),
                 static_cast<unsigned long long>(s.published_size()));
    std::fflush(stdout);

    // Stay alive until the verifier releases us (keep mapping open).
    for (int i = 0; i < 600; ++i) {
        if (std::filesystem::exists(release)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    // Cleanup: unmap/close, then remove coordination files.
    s.shutdown();
    std::error_code ec;
    std::filesystem::remove(ready, ec);
    std::filesystem::remove(meta, ec);
    std::filesystem::remove(release, ec);
    return 0;
}
