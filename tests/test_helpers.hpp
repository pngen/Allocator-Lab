#pragma once
// Allocator Lab 1.0.0 test helpers.

#include "allocator_lab/allocators.hpp"
#include "allocator_lab/allocator_registry.hpp"
#include "allocator_lab/error.hpp"
#include "allocator_lab/trace.hpp"
#include "allocator_lab/workload.hpp"

#include <memory>
#include <string>

namespace al_test_helpers {

inline std::unique_ptr<allocator_lab::AllocatorStrategy> make_allocator(const std::string& name) {
    using namespace allocator_lab;
    if (name == "system") return std::make_unique<SystemAllocator>();
    if (name == "aligned") return std::make_unique<AlignedAllocator>();
    if (name == "fixed_pool") return std::make_unique<FixedPool>(64, 1u << 20);
    if (name == "size_class_pool") return std::make_unique<SizeClassPool>();
    if (name == "slab") return std::make_unique<SlabAllocator>(256);
    if (name == "arena") return std::make_unique<ArenaAllocator>();
    if (name == "free_list") return std::make_unique<FreeListAllocator>();
    if (name == "buddy") return std::make_unique<BuddyAllocator>(22);
    if (name == "segregated_fit") return std::make_unique<SegregatedAllocator>();
    if (name == "pinned") return std::make_unique<PinnedAllocator>(256ull << 20);
    if (name == "cuda") return std::make_unique<CudaDirectAllocator>();
    if (name == "cuda_pool") return std::make_unique<CudaPoolAllocator>();
    return nullptr;
}

inline std::vector<std::string> all_normal_names() {
    return { "system", "aligned", "fixed_pool", "size_class_pool", "slab", "arena", "free_list", "buddy", "segregated_fit" };
}

// Allocators that support per-object free (arena and shared/mapped are arena-like).
inline std::vector<std::string> all_freeable_names() {
    return { "system", "aligned", "fixed_pool", "size_class_pool", "slab", "free_list", "buddy", "segregated_fit" };
}

} // namespace al_test_helpers
