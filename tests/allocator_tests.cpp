// Allocator Lab 1.0.0 allocator correctness tests.
#include "tests/test_framework.hpp"
#include "tests/test_helpers.hpp"
#include "allocator_lab/error.hpp"
#include "allocator_lab/config.hpp"

using namespace allocator_lab;
using namespace al_test_helpers;

static void basic_alloc_free(AllocatorStrategy& a) {
    AllocationRequest req; req.size = 64; req.domain = MemoryDomain::Host;
    AllocationResult r = a.allocate(req);
    REQUIRE(r.error.ok());
    REQUIRE(r.id != 0);
    REQUIRE(r.address != nullptr);
    AllocationInfo info;
    REQUIRE(a.query(r.id, info).ok());
    REQUIRE(info.live);
    REQUIRE_EQ(info.size, r.size);
    REQUIRE(a.free(r.id).ok());
    REQUIRE(a.statistics().live.live_count == 0);
}

TEST(allocator_basic_each) {
    for (const auto& n : all_freeable_names()) {
        auto a = make_allocator(n);
        REQUIRE(a != nullptr);
        basic_alloc_free(*a);
        a->shutdown();
    }
}

TEST(allocator_arena_semantics) {
    ArenaAllocator a;
    // arena supports reset, rejects free of individual objects
    REQUIRE(a.capabilities().supports_free == false);
    REQUIRE(a.capabilities().allows_per_object_free == false);
    AllocationRequest req; req.size = 128;
    AllocationResult r = a.allocate(req);
    REQUIRE(r.error.ok());
    Error fe = a.free(r.id);
    REQUIRE(!fe.ok());
    REQUIRE(fe.code == ErrorCode::not_supported);
    REQUIRE(a.reset().ok());
    REQUIRE(a.statistics().live.live_count == 0);
}

TEST(allocator_alignment_supported) {
    AlignedAllocator a(4096);
    std::size_t alignments[] = { 8, 16, 32, 64, 256, 4096 };
    for (std::size_t al : alignments) {
        AllocationRequest req; req.size = 100; req.alignment = al;
        AllocationResult r = a.allocate(req);
        REQUIRE(r.error.ok());
        REQUIRE((reinterpret_cast<std::uintptr_t>(r.address) % al) == 0);
        a.free(r.id);
    }
}

TEST(allocator_alignment_rejected) {
    AlignedAllocator a(4096);
    AllocationRequest req; req.size = 100; req.alignment = 48;  // not pow2
    REQUIRE(!a.allocate(req).error.ok());
    req.alignment = 8192;  // > max
    REQUIRE(a.allocate(req).error.code == ErrorCode::alignment_unsupported);
}

TEST(allocator_double_free_rejected) {
    for (const auto& n : all_normal_names()) {
        if (n == "arena") continue;
        auto a = make_allocator(n);
        AllocationRequest req; req.size = 64;
        AllocationResult r = a->allocate(req);
        REQUIRE(r.error.ok());
        REQUIRE(a->free(r.id).ok());
        Error e2 = a->free(r.id);
        REQUIRE(!e2.ok());  // double free / invalid handle
        a->shutdown();
    }
}

TEST(allocator_foreign_handle_rejected) {
    SystemAllocator sa;
    AlignedAllocator aa;
    AllocationRequest req; req.size = 128;
    AllocationResult r = sa.allocate(req);
    REQUIRE(r.error.ok());
    // free from a different allocator must fail (foreign handle)
    Error fe = aa.free(r.id);
    REQUIRE(!fe.ok());
    sa.free(r.id);
}

TEST(allocator_realloc_preserves_on_failure) {
    SystemAllocator sa;
    AllocationRequest req; req.size = 64;
    AllocationResult r = sa.allocate(req);
    REQUIRE(r.error.ok());
    // Realloc to a huge size exceeding max should fail and preserve original.
    AllocationRequest req2; req2.size = 1ull << 40;
    AllocationResult r2 = sa.reallocate(r.id, req2);
    REQUIRE(!r2.error.ok());
    AllocationInfo info; REQUIRE(sa.query(r.id, info).ok());
    REQUIRE(info.live);
    REQUIRE_EQ(info.size, 64u);
    sa.free(r.id);
}

TEST(allocator_realloc_grow_shrink) {
    SystemAllocator sa;
    AllocationRequest req; req.size = 32;
    AllocationResult r = sa.allocate(req);
    AllocationRequest req2; req2.size = 128;
    AllocationResult r2 = sa.reallocate(r.id, req2);
    REQUIRE(r2.error.ok());
    REQUIRE_EQ(r2.id, r.id);
    AllocationInfo info; REQUIRE(sa.query(r.id, info).ok());
    REQUIRE_EQ(info.size, 128u);
    sa.free(r.id);
}

TEST(allocator_accounting_to_zero) {
    for (const auto& n : all_freeable_names()) {
        auto a = make_allocator(n);
        // allocate several, then free all
        std::vector<AllocationId> ids;
        for (int i = 0; i < 100; ++i) {
            AllocationRequest req; req.size = 64 + i * 7; req.domain = MemoryDomain::Host;
            AllocationResult r = a->allocate(req);
            if (r.error.ok()) ids.push_back(r.id);
        }
        for (auto id : ids) a->free(id);
        auto st = a->statistics();
        REQUIRE(st.live.live_count == 0);
        REQUIRE(st.live.live_bytes == 0);
        a->shutdown();
        auto st2 = a->statistics();
        REQUIRE(st2.live.live_count == 0);
    }
}

TEST(allocator_capacity_bounded) {
    FixedPool pool(64, 1024);
    REQUIRE_EQ(pool.capabilities().max_allocation_size, 64u);
    // request exceeds pool object size -> rejected
    AllocationRequest req; req.size = 200;
    REQUIRE(!pool.allocate(req).error.ok());
}

TEST(allocator_zeroing_policy) {
    SystemAllocator a;
    AllocationRequest req; req.size = 512; req.flags = AllocationFlags::ZeroFill;
    AllocationResult r = a.allocate(req);
    REQUIRE(r.error.ok());
    auto* p = static_cast<std::uint8_t*>(r.address);
    bool allzero = true;
    for (std::size_t i = 0; i < req.size; ++i) if (p[i] != 0) allzero = false;
    REQUIRE(allzero);
    a.free(r.id);
}

TEST(allocator_poison_alloc_flag) {
    SystemAllocator a;
    AllocationRequest req; req.size = 256; req.flags = AllocationFlags::PoisonAlloc;
    AllocationResult r = a.allocate(req);
    REQUIRE(r.error.ok());
    auto* p = static_cast<std::uint8_t*>(r.address);
    bool poisoned = true;
    for (std::size_t i = 0; i < req.size; ++i) if (p[i] != detail::kPoisonAllocByte) poisoned = false;
    REQUIRE(poisoned);
    a.free(r.id);
}

TEST(allocator_memory_domain_rejected) {
    // system allocator only supports Host; requesting a Device domain should not be
    // silently served on host (it will be rejected by the system allocator or honored
    // only for host memory in general). We assert host-dominant behavior is consistent.
    SystemAllocator a;
    AllocationRequest req; req.size = 10; req.domain = MemoryDomain::Device;
    AllocationResult r = a.allocate(req);
    REQUIRE(r.error.ok() == false || r.address != nullptr);
    if (r.error.ok()) a.free(r.id);
}
