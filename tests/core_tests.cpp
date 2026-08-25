// Allocator Lab 1.0.0 core tests: ids/types, config validation, registry, error.
#include "tests/test_framework.hpp"
#include "tests/test_helpers.hpp"
#include "allocator_lab/identity.hpp"
#include "allocator_lab/config.hpp"
#include "allocator_lab/error.hpp"

using namespace allocator_lab;

TEST(core_ids_are_stable_and_increment) {
    IdGenerator g;
    std::uint64_t a = g.next();
    std::uint64_t b = g.next();
    REQUIRE(a >= 1);
    REQUIRE(b == a + 1);
}

TEST(core_alignment_is_power_of_two) {
    REQUIRE(is_power_of_two(1));
    REQUIRE(is_power_of_two(16));
    REQUIRE(is_power_of_two(4096));
    REQUIRE(!is_power_of_two(0));
    REQUIRE(!is_power_of_two(3));
    REQUIRE(!is_power_of_two(100));
}

TEST(core_round_up_pow2) {
    REQUIRE_EQ(round_up_pow2(1), 1ull);
    REQUIRE_EQ(round_up_pow2(17), 32ull);
    REQUIRE_EQ(round_up_pow2(64), 64ull);
    REQUIRE_EQ(round_up_pow2(65), 128ull);
}

TEST(core_error_code_names) {
    REQUIRE_EQ(std::string(error_code_name(ErrorCode::none)), std::string("none"));
    REQUIRE_EQ(std::string(error_code_name(ErrorCode::out_of_memory)), std::string("out_of_memory"));
    REQUIRE(error_code_name(ErrorCode::invalid_handle)[0] != '\0');
}

TEST(core_error_struct) {
    Error ok;
    REQUIRE(ok.ok());
    REQUIRE(!ok);
    Error e = make_error(ErrorCode::invalid_alignment, "bad alignment");
    REQUIRE(!e.ok());
    REQUIRE(e.code == ErrorCode::invalid_alignment);
}

TEST(core_validate_alignment) {
    REQUIRE(validate_alignment(16).ok());
    REQUIRE(!validate_alignment(0).ok());
    REQUIRE(validate_alignment(0).code == ErrorCode::invalid_alignment);
    REQUIRE(!validate_alignment(24).ok());
}

TEST(core_registry_register_and_find) {
    AllocatorRegistry reg;
    auto id = reg.register_allocator(std::make_unique<SystemAllocator>());
    REQUIRE(id != 0);
    REQUIRE(reg.contains(id));
    REQUIRE(reg.size() == 1);
    auto ptr = reg.find(id);
    REQUIRE(ptr != nullptr);
    REQUIRE_EQ(ptr->id(), id);
    REQUIRE(reg.unregister(id));
    REQUIRE(reg.size() == 0);
    REQUIRE(reg.find(id) == nullptr);
}

TEST(core_registry_explicit_id) {
    AllocatorRegistry reg;
    Error e = reg.register_allocator_at(std::make_unique<SystemAllocator>(), 0);
    REQUIRE(!e.ok());  // id 0 reserved
    e = reg.register_allocator_at(std::make_unique<SystemAllocator>(), 42);
    REQUIRE(e.ok());
    REQUIRE(reg.contains(42));
    e = reg.register_allocator_at(std::make_unique<SystemAllocator>(), 42);
    REQUIRE(!e.ok());  // duplicate
}

TEST(core_registry_for_each_no_snapshot_race) {
    AllocatorRegistry reg;
    for (int i = 0; i < 10; ++i) reg.register_allocator(std::make_unique<SystemAllocator>());
    std::size_t n = 0;
    reg.for_each([&](AllocatorStrategy&) { ++n; });
    REQUIRE_EQ(n, 10u);
}

TEST(core_builtin_register) {
    AllocatorRegistry reg;
    std::size_t n = register_builtin_allocators(reg);
    REQUIRE(n >= 9);
    // Each builtin has a unique id.
    auto ids = reg.ids();
    std::size_t unique = 0;
    std::vector<AllocatorId> seen;
    for (auto id : ids) {
        bool dup = false;
        for (auto s : seen) if (s == id) dup = true;
        if (!dup) { seen.push_back(id); ++unique; }
    }
    REQUIRE_EQ(unique, ids.size());
}
