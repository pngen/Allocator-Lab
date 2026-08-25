#pragma once

// Allocator Lab 1.0.0
// Allocator registry: stable-id registration and lookup.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "allocator_lab/allocator.hpp"
#include "allocator_lab/identity.hpp"
#include "allocator_lab/error.hpp"

namespace allocator_lab {

/// Thread-safe registry mapping a stable AllocatorId to a strategy instance.
///
/// Reads (lookup/iterate) take a shared lock; registration/take takes a unique
/// lock. No user callbacks are invoked while a lock is held.
class AllocatorRegistry {
public:
    AllocatorRegistry() = default;
    AllocatorRegistry(const AllocatorRegistry&) = delete;
    AllocatorRegistry& operator=(const AllocatorRegistry&) = delete;

    /// Register a strategy under a fresh id. Returns the assigned id, or 0 on
    /// allocation failure (which cannot normally occur given the registry only
    /// stores the strategy).
    AllocatorId register_allocator(std::unique_ptr<AllocatorStrategy> strategy) {
        std::unique_lock lock(mutex_);
        AllocatorId id = strategy->id();
        if (id == 0) {
            id = ids_.next();
            strategy->set_id(id);
        }
        id_to_strategy_.emplace(id, std::move(strategy));
        return id;
    }

    /// Register a strategy under an explicit caller-provided id. Fails if the id
    /// is 0 or already in use.
    Error register_allocator_at(std::unique_ptr<AllocatorStrategy> strategy, AllocatorId requested_id) {
        if (requested_id == 0)
            return make_error(ErrorCode::invalid_argument, "allocator id 0 is reserved");
        std::unique_lock lock(mutex_);
        if (id_to_strategy_.count(requested_id) != 0)
            return make_error(ErrorCode::already_live, "allocator id already registered");
        strategy->set_id(requested_id);
        id_to_strategy_.emplace(requested_id, std::move(strategy));
        return Error{};
    }

    /// Look up a strategy by id. Returns nullptr if not registered.
    AllocatorStrategy* find(AllocatorId id) const {
        std::shared_lock lock(mutex_);
        auto it = id_to_strategy_.find(id);
        return it == id_to_strategy_.end() ? nullptr : it->second.get();
    }

    /// Remove and destroy a strategy. Returns false if not present.
    bool unregister(AllocatorId id) {
        std::unique_lock lock(mutex_);
        return id_to_strategy_.erase(id) != 0;
    }

    bool contains(AllocatorId id) const {
        std::shared_lock lock(mutex_);
        return id_to_strategy_.count(id) != 0;
    }

    std::size_t size() const {
        std::shared_lock lock(mutex_);
        return id_to_strategy_.size();
    }

    std::vector<AllocatorId> ids() const {
        std::shared_lock lock(mutex_);
        std::vector<AllocatorId> out;
        out.reserve(id_to_strategy_.size());
        for (const auto& kv : id_to_strategy_) out.push_back(kv.first);
        return out;
    }

    /// Iterate all strategies. The callback is invoked WITHOUT the lock held.
    template <typename F>
    void for_each(F&& f) const {
        std::vector<AllocatorStrategy*> snapshot;
        {
            std::shared_lock lock(mutex_);
            snapshot.reserve(id_to_strategy_.size());
            for (const auto& kv : id_to_strategy_) snapshot.push_back(kv.second.get());
        }
        for (auto* s : snapshot) f(*s);
    }

    /// Destroy every registered strategy and clear the registry.
    void clear() {
        std::unique_lock lock(mutex_);
        id_to_strategy_.clear();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<AllocatorId, std::unique_ptr<AllocatorStrategy>> id_to_strategy_;
    IdGenerator ids_;
};

} // namespace allocator_lab
