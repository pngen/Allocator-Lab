#pragma once

// Allocator Lab 1.0.0
// Stable, thread-safe id generation.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <atomic>
#include <cstdint>

#include "allocator_lab/types.hpp"

namespace allocator_lab {

/// Monotonic, thread-safe generator of never-reused 64-bit ids.
class IdGenerator {
public:
    explicit IdGenerator(const IdGenerator&) = delete;
    IdGenerator& operator=(const IdGenerator&) = delete;

    IdGenerator() = default;

    /// Fetch the next id. Ids are >= 1 (0 is reserved as "no id").
    std::uint64_t next() noexcept {
        return next_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

private:
    std::atomic<std::uint64_t> next_{0};
};

} // namespace allocator_lab
