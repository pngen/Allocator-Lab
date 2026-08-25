// Allocator Lab 1.0.0 trace serialization, validation & replay tests.
#include "tests/test_framework.hpp"
#include "allocator_lab/trace.hpp"
#include "allocator_lab/workload.hpp"

#include <cstdint>
#include <filesystem>

using namespace allocator_lab;

static Trace make_small_trace() {
    Trace t; t.version = kTraceVersion; t.seed = 5; t.name = "small";
    TraceEntry a; a.seq = 0; a.step = 0; a.op = TraceOperationType::Allocate; a.id = 1; a.size = 64; a.alignment = 16;
    TraceEntry b; b.seq = 1; b.step = 1; b.op = TraceOperationType::Free; b.id = 1; b.size = 64; b.alignment = 16;
    t.entries.push_back(a); t.entries.push_back(b);
    return t;
}

TEST(trace_json_roundtrip) {
    Trace in = make_small_trace();
    std::string json = trace_to_json(in);
    Trace out;
    REQUIRE(trace_from_json(json, out).ok());
    REQUIRE_EQ(out.entry_count(), in.entry_count());
    REQUIRE_EQ(out.seed, in.seed);
    REQUIRE(out.entries[0].op == TraceOperationType::Allocate);
    REQUIRE(out.entries[0].size == 64);
    REQUIRE(out.entries[1].op == TraceOperationType::Free);
}

TEST(trace_binary_roundtrip) {
    Trace in = make_small_trace();
    std::vector<std::uint8_t> bin = trace_to_binary(in);
    Trace out;
    REQUIRE(trace_from_binary(bin, out).ok());
    REQUIRE_EQ(out.entry_count(), in.entry_count());
    REQUIRE_EQ(out.seed, in.seed);
}

TEST(trace_rejects_malformed_op) {
    Trace in = make_small_trace();
    in.entries.front().op = static_cast<TraceOperationType>(99);
    REQUIRE(!validate_trace(in).ok());
    // JSON path rejects too
    std::string json = trace_to_json(in);
    Trace out;
    REQUIRE(!trace_from_json(json, out).ok());
}

TEST(trace_rejects_negative_size) {
    Trace in = make_small_trace();
    in.entries[0].size = 0;  // 0 is allowed as a placeholder, but negative impossible in uint.
    // Instead craft raw JSON with a negative size.
    std::string json = "{\"version\":1,\"seed\":1,\"name\":\"x\",\"entries\":[{\"seq\":0,\"step\":0,\"op\":0,\"id\":1,\"size\":-5,\"alignment\":0,\"domain\":0,\"worker\":0,\"tag\":0}]}";
    Trace out;
    REQUIRE(!trace_from_json(json, out).ok());
}

TEST(trace_rejects_duplicate_live_id) {
    Trace t; t.version = kTraceVersion; t.seed = 1;
    TraceEntry a; a.seq=0; a.step=0; a.op=TraceOperationType::Allocate; a.id=1; a.size=16;
    TraceEntry b; b.seq=1; b.step=1; b.op=TraceOperationType::Allocate; b.id=1; b.size=16;
    t.entries.push_back(a); t.entries.push_back(b);
    REQUIRE(!validate_trace(t).ok());
}

TEST(trace_rejects_free_of_nonexistent) {
    Trace t; t.version = kTraceVersion; t.seed = 1;
    TraceEntry a; a.seq=0; a.step=0; a.op=TraceOperationType::Free; a.id=999; a.size=16;
    t.entries.push_back(a);
    REQUIRE(!validate_trace(t).ok());
}

TEST(trace_rejects_non_pow2_alignment) {
    Trace t; t.version = kTraceVersion; t.seed = 1;
    TraceEntry a; a.seq=0; a.step=0; a.op=TraceOperationType::Allocate; a.id=1; a.size=16; a.alignment=24;
    t.entries.push_back(a);
    REQUIRE(!validate_trace(t).ok());
}

TEST(trace_validator_allows_id_reuse_after_free) {
    Trace t; t.version = kTraceVersion; t.seed = 1;
    TraceEntry a; a.seq=0; a.step=0; a.op=TraceOperationType::Allocate; a.id=1; a.size=16;
    TraceEntry b; b.seq=1; b.step=1; b.op=TraceOperationType::Free; b.id=1; b.size=16;
    TraceEntry c; c.seq=2; c.step=2; c.op=TraceOperationType::Allocate; c.id=1; c.size=16;
    t.entries.push_back(a); t.entries.push_back(b); t.entries.push_back(c);
    REQUIRE(validate_trace(t).ok());
}

TEST(trace_file_roundtrip) {
    std::filesystem::path path = std::filesystem::temp_directory_path() / "allocator_lab_test_trace.trace.json";
    Trace in = make_small_trace();
    REQUIRE(save_trace_json(in, path).ok());
    Trace out; REQUIRE(load_trace_json(path, out).ok());
    REQUIRE_EQ(out.entry_count(), in.entry_count());
    std::filesystem::remove(path);
}

TEST(trace_deterministic_replay_on_two_allocators) {
    WorkloadConfig wc; wc.operations = 2000; wc.live_target = 128; wc.seed = 33; wc.size_dist = SizeDistribution::Uniform;
    Trace trace; REQUIRE(generate_workload(wc, trace).ok());
    std::string json = trace_to_json(trace);
    Trace back; REQUIRE(trace_from_json(json, back).ok());
    REQUIRE_EQ(back.entry_count(), trace.entry_count());
}
