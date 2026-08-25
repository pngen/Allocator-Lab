#pragma once

// Allocator Lab 1.0.0
// Deterministic seeded PRNG (SplitMix64). Used to make every workload and trace
// fully reproducible from a single seed.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <cmath>

namespace allocator_lab::detail {

/// SplitMix64: fast, high-quality, deterministic, trivially seedable.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

    std::uint64_t next() noexcept {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    /// Uniform in [0, bound). bound must be > 0.
    std::uint64_t next(std::uint64_t bound) noexcept {
        return bound == 0 ? 0 : next() % bound;
    }

    /// Uniform in [lo, hi] inclusive. Precondition: lo <= hi.
    std::uint64_t next_in(std::uint64_t lo, std::uint64_t hi) noexcept {
        return lo + next(hi - lo + 1);
    }

    /// Uniform double in [0,1).
    double next01() noexcept {
        return (next() >> 11) * (1.0 / 9007199254740992.0);
    }

    /// Update the seed (for sub-streams) without disturbing the caller's state.
    std::uint64_t seed() const noexcept { return state_; }

private:
    std::uint64_t state_;
};

} // namespace allocator_lab::detail
