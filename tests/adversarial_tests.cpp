// Allocator Lab 1.0.0 adversarial hardening tests.
#include "tests/test_framework.hpp"
#include "tests/test_helpers.hpp"
#include "allocator_lab/error.hpp"
#include "allocator_lab/config.hpp"
#include <cstring>
#include <vector>

using namespace allocator_lab;
using namespace al_test_helpers;

TEST(adversarial_payload_integrity) {
    SystemAllocator a;
    for (int i = 0; i < 1000; ++i) {
        std::size_t sz = 1 + static_cast<std::size_t>(i % 512);
        AllocationRequest req; req.size = sz;
        AllocationResult r = a.allocate(req);
        REQUIRE(r.error.ok());
        auto* p = static_cast<std::uint8_t*>(r.address);
        std::memset(p, static_cast<int>(i & 0xFF), sz);
        // verify before free
        bool ok = true; for (std::size_t j = 0; j < sz; ++j) if (p[j] != static_cast<std::uint8_t>(i & 0xFF)) ok = false;
        REQUIRE(ok);
        REQUIRE(a.free(r.id).ok());
    }
}

TEST(adversarial_capacity_exhaustion_accounting) {
    FixedPool pool(64, 256);   // capacity 256 objects
    std::vector<AllocationId> ids;
    AllocationRequest req; req.size = 64;
    for (int i = 0; i < 3000 && ids.size() < 256; ++i) {
        AllocationResult r = pool.allocate(req);
        if (r.error.ok()) ids.push_back(r.id);
        else REQUIRE(r.error.code == ErrorCode::capacity_exceeded);
    }
    REQUIRE_EQ(ids.size(), 256u);
    // pushing past capacity must fail and leave accounting unchanged
    AllocatorStatistics before = pool.statistics();
    AllocationResult over = pool.allocate(req);
    REQUIRE(!over.error.ok());
    AllocatorStatistics after = pool.statistics();
    REQUIRE_EQ(after.live.live_count, before.live.live_count);
    // clean up
    for (auto id : ids) REQUIRE(pool.free(id).ok());
    REQUIRE_EQ(pool.statistics().live.live_count, 0u);
}

TEST(adversarial_stale_handle_rejected) {
    SystemAllocator a;
    AllocationRequest req; req.size = 128;
    AllocationResult r = a.allocate(req);
    REQUIRE(r.error.ok());
    REQUIRE(a.free(r.id).ok());
    AllocationInfo info;
    REQUIRE(!a.query(r.id, info).ok());  // stale handle
}

TEST(adversarial_zero_size_behavior) {
    SystemAllocator a;
    AllocationRequest req; req.size = 0;
    AllocationResult r = a.allocate(req);
    // Should either succeed (1 byte) or fail cleanly; must not crash or corrupt.
    if (r.error.ok()) REQUIRE(a.free(r.id).ok());
    else REQUIRE(!r.error.ok());
}

TEST(adversarial_max_size_boundary) {
    SystemAllocator a;
    std::size_t mx = a.capabilities().max_allocation_size;
    AllocationRequest req; req.size = mx;
    AllocationResult r = a.allocate(req);
    REQUIRE(r.error.ok());
    a.free(r.id);
    AllocationRequest req2; req2.size = mx + 1;
    AllocationResult r2 = a.allocate(req2);
    REQUIRE(!r2.error.ok());
}

TEST(adversarial_repeated_grow_shrink_realloc) {
    AlignedAllocator a(4096);
    AllocationRequest req; req.size = 16;
    AllocationResult r = a.allocate(req);
    REQUIRE(r.error.ok());
    for (int i = 0; i < 200; ++i) {
        std::size_t n = 16 + (i % 5) * 64;
        AllocationRequest rr; rr.size = n;
        AllocationResult r2 = a.reallocate(r.id, rr);
        REQUIRE(r2.error.ok());
    }
    a.free(r.id);
}

TEST(adversarial_trim_reset_cycles) {
    ArenaAllocator a;
    for (int cycle = 0; cycle < 50; ++cycle) {
        std::vector<AllocationId> ids;
        for (int i = 0; i < 200; ++i) {
            AllocationRequest req; req.size = 32 + i;
            AllocationResult r = a.allocate(req);
            REQUIRE(r.error.ok());
            ids.push_back(r.id);
        }
        REQUIRE(a.statistics().live.live_count == 200);
        REQUIRE(a.reset().ok());
        REQUIRE(a.statistics().live.live_count == 0);
        for (auto id : ids) { AllocationInfo info; REQUIRE(!a.query(id, info).ok()); }  // reset invalidates stale handles
    }
}

TEST(adversarial_pool_reuse_no_poison_leak) {
    SlabAllocator a(64);
    std::vector<AllocationId> ids;
    for (int i = 0; i < 1000; ++i) {
        AllocationRequest req; req.size = 64;
        AllocationResult r = a.allocate(req);
        REQUIRE(r.error.ok());
        ids.push_back(r.id);
    }
    for (auto id : ids) REQUIRE(a.free(id).ok());
    // Reusable backing should be reused on next alloc (pool hit).
    AllocationRequest req; req.size = 64;
    AllocationResult r = a.allocate(req);
    REQUIRE(r.error.ok());
    REQUIRE(r.reused_backing);
    a.free(r.id);
}
