#pragma once

// Allocator Lab 1.0.0
// Bounded, thread-safe structured event log.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "allocator_lab/types.hpp"

namespace allocator_lab {

enum class EventType : std::uint16_t {
    ExperimentStarted = 0,
    ExperimentCompleted = 1,
    ExperimentFailed = 2,
    AllocatorRegistered = 3,
    AllocationSucceeded = 4,
    AllocationFailed = 5,
    FreeSucceeded = 6,
    FreeFailed = 7,
    ReallocSucceeded = 8,
    ReallocFailed = 9,
    CapacityReached = 10,
    PoolGrown = 11,
    PoolTrimmed = 12,
    FragmentationSample = 13,
    TraceCaptured = 14,
    TraceReplayStarted = 15,
    TraceReplayCompleted = 16,
    FailureInjected = 17,
    InvariantFailed = 18,
    ReportWritten = 19,
};

inline const char* event_type_name(EventType t) noexcept {
    switch (t) {
        case EventType::ExperimentStarted: return "experiment_started";
        case EventType::ExperimentCompleted: return "experiment_completed";
        case EventType::ExperimentFailed: return "experiment_failed";
        case EventType::AllocatorRegistered: return "allocator_registered";
        case EventType::AllocationSucceeded: return "allocation_succeeded";
        case EventType::AllocationFailed: return "allocation_failed";
        case EventType::FreeSucceeded: return "free_succeeded";
        case EventType::FreeFailed: return "free_failed";
        case EventType::ReallocSucceeded: return "realloc_succeeded";
        case EventType::ReallocFailed: return "realloc_failed";
        case EventType::CapacityReached: return "capacity_reached";
        case EventType::PoolGrown: return "pool_grown";
        case EventType::PoolTrimmed: return "pool_trimmed";
        case EventType::FragmentationSample: return "fragmentation_sample";
        case EventType::TraceCaptured: return "trace_captured";
        case EventType::TraceReplayStarted: return "trace_replay_started";
        case EventType::TraceReplayCompleted: return "trace_replay_completed";
        case EventType::FailureInjected: return "failure_injected";
        case EventType::InvariantFailed: return "invariant_failed";
        case EventType::ReportWritten: return "report_written";
    }
    return "unknown";
}

/// A structured event. Timestamps are monotonic steady_clock time points.
struct Event {
    EventId id = 0;
    EventType type = EventType::ExperimentStarted;
    std::uint64_t step = 0;
    WorkerId worker = 0;
    std::chrono::steady_clock::time_point timestamp{};
    std::string message;
};

/// A bounded, thread-safe event log.
///
/// Events are recorded under an internal lock but the optional sink callback is
/// invoked AFTER the lock is released, so no user callback ever runs while an
/// internal lab lock is held (self-deadlock and reentrancy audit requirement).
class EventLog {
public:
    explicit EventLog(std::uint64_t capacity = 1ull << 20)
        : capacity_(capacity) {}

    void emit(EventType type, std::uint64_t step = 0, WorkerId worker = 0,
              std::string message = {}) {
        Event ev;
        ev.id = next_id_.fetch_add(1, std::memory_order_relaxed) + 1;
        ev.type = type;
        ev.step = step;
        ev.worker = worker;
        ev.timestamp = std::chrono::steady_clock::now();
        ev.message = std::move(message);
        {
            std::lock_guard lock(mutex_);
            if (events_.size() < capacity_) events_.push_back(std::move(ev));
            else { dropped_++; }
        }
        // Invoke sink outside the lock.
        Sink sink_copy;
        {
            std::lock_guard lock(mutex_);
            sink_copy = sink_;
        }
        if (sink_copy) sink_copy(ev);
    }

    using Sink = std::function<void(const Event&)>;

    void set_sink(Sink sink) {
        std::lock_guard lock(mutex_);
        sink_ = std::move(sink);
    }

    std::vector<Event> snapshot() const {
        std::lock_guard lock(mutex_);
        return events_;
    }

    std::uint64_t count() const {
        std::lock_guard lock(mutex_);
        return static_cast<std::uint64_t>(events_.size());
    }
    std::uint64_t dropped() const {
        std::lock_guard lock(mutex_);
        return dropped_;
    }
    void clear() {
        std::lock_guard lock(mutex_);
        events_.clear();
        dropped_ = 0;
    }

private:
    std::uint64_t capacity_;
    std::atomic<std::uint64_t> next_id_{0};
    mutable std::mutex mutex_;
    std::vector<Event> events_;
    std::uint64_t dropped_ = 0;
    Sink sink_;
};

} // namespace allocator_lab
