#pragma once

// Allocator Lab 1.0.0
// Aggregated include for all built-in allocator strategies.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "allocator_lab/allocators/fixed_pool.hpp"
#include "allocator_lab/allocators/size_class_pool.hpp"
#include "allocator_lab/allocators/slab_allocator.hpp"
#include "allocator_lab/allocators/arena_allocator.hpp"
#include "allocator_lab/allocators/free_list_allocator.hpp"
#include "allocator_lab/allocators/buddy_allocator.hpp"
#include "allocator_lab/allocators/segregated_allocator.hpp"
#include "allocator_lab/allocators/system_allocator.hpp"
#include "allocator_lab/allocators/aligned_allocator.hpp"
#include "allocator_lab/allocators/pinned_allocator.hpp"
#include "allocator_lab/allocators/cuda_allocator.hpp"
#include "allocator_lab/allocators/cuda_pool_allocator.hpp"
#include "allocator_lab/allocators/shared_allocator.hpp"
#include "allocator_lab/allocators/mapped_allocator.hpp"
#include "allocator_lab/allocator_registry.hpp"

namespace allocator_lab {

/// Register every built-in allocator into a registry. Strategies that require
/// an unavailable backend (e.g. CUDA) are still registered but report the
/// backend as unavailable via their capabilities. Returns the number registered.
inline std::size_t register_builtin_allocators(AllocatorRegistry& registry) {
    std::size_t n = 0;
    n += registry.register_allocator(std::make_unique<SystemAllocator>()) != 0;
    n += registry.register_allocator(std::make_unique<AlignedAllocator>()) != 0;
    n += registry.register_allocator(std::make_unique<FixedPool>(64, 1u << 20)) != 0;
    n += registry.register_allocator(std::make_unique<SizeClassPool>()) != 0;
    n += registry.register_allocator(std::make_unique<SlabAllocator>(256)) != 0;
    n += registry.register_allocator(std::make_unique<ArenaAllocator>()) != 0;
    n += registry.register_allocator(std::make_unique<FreeListAllocator>()) != 0;
    n += registry.register_allocator(std::make_unique<BuddyAllocator>(24)) != 0;
    n += registry.register_allocator(std::make_unique<SegregatedAllocator>()) != 0;
    n += registry.register_allocator(std::make_unique<PinnedAllocator>()) != 0;
    n += registry.register_allocator(std::make_unique<CudaDirectAllocator>()) != 0;
    n += registry.register_allocator(std::make_unique<CudaPoolAllocator>()) != 0;
    return n;
}

} // namespace allocator_lab
