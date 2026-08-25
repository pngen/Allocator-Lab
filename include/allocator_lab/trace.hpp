#pragma once

// Allocator Lab 1.0.0
// Versioned allocation trace format: capture, serialize, validate, replay.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

#include "allocator_lab/types.hpp"
#include "allocator_lab/error.hpp"

namespace allocator_lab {

// Current trace format version.
constexpr std::uint32_t kTraceVersion = 1;
constexpr std::uint32_t kTraceMagic = 0x414C5452;

// One trace entry. id is a stable logical allocation id (never a raw
// address) so a stored trace remains meaningful and replayable across runs.
struct TraceEntry {
    std::uint64_t seq = 0;
    std::uint64_t step = 0;
    TraceOperationType op = TraceOperationType::Allocate;
    AllocationId id = 0;
    std::uint64_t size = 0;
    std::uint64_t alignment = 0;
    MemoryDomain domain = MemoryDomain::Host;
    WorkerId worker = 0;
    std::uint32_t tag = 0;
    bool has_status = false;
    bool status_success = false;
    std::uint16_t error_code = 0;
};

struct Trace {
    std::uint32_t version = kTraceVersion;
    std::uint64_t seed = 0;
    std::string name;
    std::vector<TraceEntry> entries;
    std::size_t entry_count() const noexcept { return entries.size(); }
    bool empty() const noexcept { return entries.empty(); }
};

std::string trace_to_json(const Trace& trace);
Error trace_from_json(const std::string& json, Trace& out);

std::vector<std::uint8_t> trace_to_binary(const Trace& trace);
Error trace_from_binary(const std::vector<std::uint8_t>& bytes, Trace& out);

Error save_trace_json(const Trace& trace, const std::filesystem::path& path);
Error load_trace_json(const std::filesystem::path& path, Trace& out);
Error save_trace_binary(const Trace& trace, const std::filesystem::path& path);
Error load_trace_binary(const std::filesystem::path& path, Trace& out);

Error validate_trace(const Trace& trace);

class TraceReplayValidator {
public:
    void reset(std::uint64_t seed);
    Error validate(const TraceEntry& e);
    std::uint64_t live_count() const noexcept { return live_; }
    std::uint64_t processed() const noexcept { return processed_; }
    bool is_live(AllocationId id) const noexcept;
private:
    std::vector<AllocationId> live_ids_;
    std::uint64_t live_ = 0;
    std::uint64_t processed_ = 0;
};

} // namespace allocator_lab
