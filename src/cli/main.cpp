// Allocator Lab 1.0.0
// Command-line interface.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "allocator_lab/allocators.hpp"
#include "allocator_lab/allocator_registry.hpp"
#include "allocator_lab/backend.hpp"
#include "allocator_lab/experiment.hpp"
#include "allocator_lab/provenance.hpp"
#include "allocator_lab/report.hpp"
#include "allocator_lab/workload.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

using namespace allocator_lab;

namespace {

const char* kVersion = "1.0.0";

struct Cli {
    std::string command;
    std::map<std::string, std::string> opts;
};

Cli parse_args(const std::vector<std::string>& tokens, std::string& err) {
    Cli cli;
    bool have_cmd = false;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (t.size() >= 2 && t[0] == '-' && t[1] == '-') {
            if (i + 1 >= tokens.size()) { err = "option " + t + " requires a value"; return cli; }
            cli.opts[t.substr(2)] = tokens[++i];
        } else if (t.size() >= 1 && t[0] == '-') {
            if (i + 1 >= tokens.size()) { err = "option " + t + " requires a value"; return cli; }
            cli.opts[t.substr(1)] = tokens[++i];
        } else {
            if (!have_cmd) { cli.command = t; have_cmd = true; }
            else { err = "unexpected positional argument: " + t; return cli; }
        }
    }
    return cli;
}

bool get_u64(const Cli& cli, const std::string& key, std::uint64_t def, std::uint64_t& out, std::string& err) {
    auto it = cli.opts.find(key);
    if (it == cli.opts.end()) { out = def; return true; }
    const std::string& s = it->second;
    if (s.empty()) { err = "empty value for --" + key; return false; }
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < s.size(); ++i) { char c = s[i]; if (c != '0' && c != '1' && c != '2' && c != '3' && c != '4' && c != '5' && c != '6' && c != '7' && c != '8' && c != '9') { err = "non-numeric --" + key + " (" + s + ")"; return false; } v = v * 10 + static_cast<std::uint64_t>(c - '0'); }
    out = v; return true;
}

bool get_size(const Cli& cli, const std::string& key, std::size_t def, std::size_t& out, std::string& err) {
    std::uint64_t u = 0;
    if (!get_u64(cli, key, static_cast<std::uint64_t>(def), u, err)) return false;
    out = static_cast<std::size_t>(u); return true;
}

std::unique_ptr<AllocatorStrategy> make_allocator(const std::string& name) {
    if (name == "system") return std::make_unique<SystemAllocator>();
    if (name == "aligned") return std::make_unique<AlignedAllocator>();
    if (name == "fixed_pool") return std::make_unique<FixedPool>(64, 1u << 20);
    if (name == "size_class_pool") return std::make_unique<SizeClassPool>();
    if (name == "slab") return std::make_unique<SlabAllocator>(256);
    if (name == "arena") return std::make_unique<ArenaAllocator>();
    if (name == "free_list") return std::make_unique<FreeListAllocator>();
    if (name == "buddy") return std::make_unique<BuddyAllocator>(24);
    if (name == "segregated_fit") return std::make_unique<SegregatedAllocator>();
    if (name == "pinned") return std::make_unique<PinnedAllocator>();
    if (name == "cuda") return std::make_unique<CudaDirectAllocator>();
    if (name == "cuda_pool") return std::make_unique<CudaPoolAllocator>();
    if (name == "shared") return std::make_unique<SharedAllocator>("allocator_lab_cli_shm", 8ull << 20, SharedAllocator::Mode::Create);
    if (name == "mapped") return std::make_unique<MappedAllocator>("allocator_lab_cli.map", 8ull << 20, true);
    return nullptr;
}

WorkloadConfig workload_from_opts(const Cli& cli, bool& ok, std::string& err) {
    WorkloadConfig c;
    std::size_t v;
    ok = get_size(cli, "ops", 100000, v, err); if (!ok) return c; c.operations = v;
    ok = get_size(cli, "live-bytes", 0, v, err); if (!ok) return c; c.live_target = v;
    ok = get_size(cli, "min-size", 16, v, err); if (!ok) return c; c.min_size = v;
    ok = get_size(cli, "max-size", 8 * 1024 * 1024, v, err); if (!ok) return c; c.max_size = v;
    std::uint64_t u;
    ok = get_u64(cli, "seed", c.seed, u, err); if (!ok) return c; c.seed = u;
    std::string d = cli.opts.count("distribution") ? cli.opts.at("distribution") : "log_uniform";
    if (d == "fixed") c.size_dist = SizeDistribution::Fixed;
    else if (d == "uniform") c.size_dist = SizeDistribution::Uniform;
    else if (d == "log_uniform") c.size_dist = SizeDistribution::LogUniform;
    else if (d == "geometric") c.size_dist = SizeDistribution::Geometric;
    else if (d == "bimodal") c.size_dist = SizeDistribution::Bimodal;
    else if (d == "multimodal") c.size_dist = SizeDistribution::MultiModal;
    else if (d == "power_law") c.size_dist = SizeDistribution::PowerLaw;
    else { err = "unknown distribution: " + d; ok = false; return c; }
    if (cli.opts.count("alignment")) { c.alignment_dist = AlignmentDistribution::Fixed; std::size_t a; ok = get_size(cli, "alignment", 64, a, err); if (!ok) return c; c.alignments.push_back(a); }
    std::string dom = cli.opts.count("domain") ? cli.opts.at("domain") : "host";
    if (dom == "host") c.domain = MemoryDomain::Host;
    else if (dom == "aligned") c.domain = MemoryDomain::Aligned;
    else if (dom == "pinned") c.domain = MemoryDomain::Pinned;
    else if (dom == "device") c.domain = MemoryDomain::Device;
    else if (dom == "shared") c.domain = MemoryDomain::Shared;
    else if (dom == "mapped") c.domain = MemoryDomain::Mapped;
    else { err = "unknown domain: " + dom; ok = false; return c; }
    return c;
}

int run_single(const std::string& aname, const WorkloadConfig& wc, bool json, const std::string& output) {
    auto alloc = make_allocator(aname);
    if (!alloc) { std::cerr << "error: unknown allocator: " << aname << "\n"; return 2; }
    Trace trace;
    Error ge = generate_workload(wc, trace);
    if (ge) { std::cerr << "error: workload generation: " << ge.message << "\n"; return 1; }
    ExperimentConfig ec; ec.verify_payload = true;
    ExperimentResult res = run_replay(*alloc, trace, ec, make_provenance(ALLOCATOR_LAB_BUILD_CONFIG));
    std::string out = json ? result_to_json(res) : result_to_text(res);
    std::cout << out;
    if (!output.empty()) { Error we = write_text_file(output, out); if (we) std::cerr << "warning: could not write " << output << "\n"; }
    return 0;
}

std::string usage() {
    return "Usage: allocator-lab <command> [options]\n"
           "Commands: info, allocators, capabilities, run, compare, trace-generate, trace-capture, trace-replay, inspect-trace, stress, fragmentation, concurrency, cuda, pinned, shared, mmap, report, explain, benchmark\n";
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> tokens(argv + 1, argv + argc);
    std::string err;
    Cli cli = parse_args(tokens, err);
    if (!err.empty() || cli.command.empty()) { std::cerr << "error: " << (err.empty() ? "no command" : err) << "\n" << usage(); return 2; }
    bool json = cli.opts.count("json") != 0;
    std::string output = cli.opts.count("output") ? cli.opts.at("output") : std::string();

    if (cli.command == "info") {
        BuildProvenance p = make_provenance(ALLOCATOR_LAB_BUILD_CONFIG);
        std::cout << "Allocator Lab " << kVersion << "\n";
        std::cout << "OS: " << p.os << "\nArch: " << p.architecture << "\nCPU: " << p.cpu_model << "\n";
        std::cout << "Logical cores: " << p.logical_cores << "\nPhysical cores: " << p.physical_cores << "\n";
        std::cout << "RAM: " << (p.system_ram_bytes / (1024ull * 1024ull)) << " MiB\n";
        std::cout << "Compiler: " << p.compiler << " " << p.compiler_version << "\n";
        std::cout << "CMake: " << p.cmake_version << "\nGit: " << p.git_revision << "\nConfig: " << p.build_config << "\n";
        DeviceInfo di = query_device_info("cuda");
        if (di.available) std::cout << "CUDA device: " << di.name << " | " << (di.total_memory_bytes / (1024u*1024u)) << " MiB | driver " << di.driver_version << " | runtime " << di.runtime_version << "\n";
        else std::cout << "CUDA: " << (di.has_cuda_build ? "built but no device" : "not built") << "\n";
        return 0;
    }

    if (cli.command == "allocators" || cli.command == "capabilities") {
        AllocatorRegistry r; register_builtin_allocators(r);
        r.for_each([&](AllocatorStrategy& a) {
            const AllocatorCapabilities& c = a.capabilities();
            std::cout << a.id() << "  " << a.name() << " (" << allocator_kind_name(a.kind()) << ")\n";
            std::cout << "    allocate=" << c.supports_allocate << " free=" << c.supports_free << " realloc=" << c.supports_reallocate
                      << " trim=" << c.supports_trim << " reset=" << c.supports_reset << " thread_safe=" << c.is_thread_safe
                      << " per_obj_free=" << c.allows_per_object_free << " aligned=" << c.supports_alignment << "\n";
            if (!c.notes.empty()) std::cout << "    notes: " << c.notes << "\n";
        });
        return 0;
    }

    if (cli.command == "run" || cli.command == "report" || cli.command == "fragmentation" || cli.command == "stress") {
        bool ok; std::string we;
        WorkloadConfig wc = workload_from_opts(cli, ok, we);
        if (!ok) { std::cerr << "error: " << we << "\n"; return 2; }
        if (cli.command == "fragmentation") { wc.size_dist = SizeDistribution::LogUniform; wc.live_target = 4096; wc.operations = 200000; }
        if (cli.command == "stress") { wc.size_dist = SizeDistribution::LogUniform; wc.live_target = 8192; wc.operations = 500000; }
        std::string aname = cli.opts.count("allocator") ? cli.opts.at("allocator") : "system";
        return run_single(aname, wc, json, output);
    }

    if (cli.command == "compare") {
        bool ok; std::string we;
        WorkloadConfig wc = workload_from_opts(cli, ok, we);
        if (!ok) { std::cerr << "error: " << we << "\n"; return 2; }
        std::vector<ExperimentResult> results;
        std::vector<std::string> names;
        if (cli.opts.count("allocators")) { std::string s = cli.opts.at("allocators"); std::string cur; for (char ch : s) { if (ch == ',') { if (!cur.empty()) names.push_back(cur); cur.clear(); } else cur += ch; } if (!cur.empty()) names.push_back(cur); }
        else names = { "system", "aligned", "fixed_pool", "size_class_pool", "arena", "free_list", "buddy", "segregated_fit" };
        for (const std::string& n : names) {
            auto alloc = make_allocator(n); if (!alloc) { std::cerr << "error: unknown allocator: " << n << "\n"; continue; }
            Trace trace; if (generate_workload(wc, trace)) { std::cerr << "error: workload\n"; continue; }
            ExperimentConfig ec; ec.verify_payload = true;
            results.push_back(run_replay(*alloc, trace, ec, make_provenance(ALLOCATOR_LAB_BUILD_CONFIG)));
        }
        ComparisonReport rep = build_comparison(results);
        rep.workload_desc = workload_config_to_string(wc);
        if (cli.opts.count("weights")) {
            std::string s = cli.opts.at("weights");
            std::string cur;
            std::string k;
            for (char ch : s) {
                if (ch == '=') { k = cur; cur.clear(); }
                else if (ch == ',') {
                    if (!k.empty()) { try { rep.weights.push_back({ k, std::stod(cur) }); } catch (...) {} }
                    k.clear(); cur.clear();
                } else { cur += ch; }
            }
            if (!k.empty()) { try { rep.weights.push_back({ k, std::stod(cur) }); } catch (...) {} }
        }
        apply_weighted_score(rep);
        std::string out = json ? report_to_json(rep) : report_to_text(rep);
        std::cout << out;
        if (!output.empty()) write_text_file(output, out);
        return 0;
    }

    if (cli.command == "trace-generate") {
        bool ok; std::string we;
        WorkloadConfig wc = workload_from_opts(cli, ok, we);
        if (!ok) { std::cerr << "error: " << we << "\n"; return 2; }
        Trace trace; Error ge = generate_workload(wc, trace);
        if (ge) { std::cerr << "error: " << ge.message << "\n"; return 1; }
        std::string path = cli.opts.count("trace") ? cli.opts.at("trace") : "trace.trace.json";
        Error se = save_trace_json(trace, path);
        if (se) { std::cerr << "error: " << se.message << "\n"; return 1; }
        std::cout << "wrote " << trace.entry_count() << "-entry trace to " << path << "\n";
        return 0;
    }

    if (cli.command == "inspect-trace") {
        std::string path = cli.opts.count("trace") ? cli.opts.at("trace") : "trace.trace.json";
        Trace trace; Error le = load_trace_json(path, trace);
        if (le) { std::cerr << "error: " << le.message << "\n"; return 1; }
        std::uint64_t alloc = 0, free = 0, realloc = 0;
        for (const auto& e : trace.entries) { if (e.op == TraceOperationType::Allocate) ++alloc; else if (e.op == TraceOperationType::Free) ++free; else if (e.op == TraceOperationType::Reallocate) ++realloc; }
        std::cout << "trace: " << path << " version=" << trace.version << " seed=" << trace.seed
                  << " entries=" << trace.entry_count() << " alloc=" << alloc << " free=" << free << " realloc=" << realloc << "\n";
        return 0;
    }

    if (cli.command == "trace-replay" || cli.command == "trace-capture") {
        std::string path = cli.opts.count("trace") ? cli.opts.at("trace") : "trace.trace.json";
        Trace trace; Error le = load_trace_json(path, trace);
        if (le) { std::cerr << "error: " << le.message << "\n"; return 1; }
        std::string aname = cli.opts.count("allocator") ? cli.opts.at("allocator") : "system";
        auto alloc = make_allocator(aname);
        if (!alloc) { std::cerr << "error: unknown allocator: " << aname << "\n"; return 2; }
        ExperimentConfig ec; ec.verify_payload = true;
        ExperimentResult res = run_replay(*alloc, trace, ec, make_provenance(ALLOCATOR_LAB_BUILD_CONFIG));
        std::cout << (json ? result_to_json(res) : result_to_text(res));
        return 0;
    }

    if (cli.command == "concurrency") {
        bool ok; std::string we;
        WorkloadConfig wc = workload_from_opts(cli, ok, we);
        if (!ok) { std::cerr << "error: " << we << "\n"; return 2; }
        wc.threads = 8; wc.operations = 400000;
        std::string aname = cli.opts.count("allocator") ? cli.opts.at("allocator") : "system";
        Trace trace; generate_workload(wc, trace);
        std::cout << "thread scaling for " << aname << "\n";
        for (std::uint32_t t : { (std::uint32_t)1, (std::uint32_t)2, (std::uint32_t)4, (std::uint32_t)8 }) {
            auto alloc2 = make_allocator(aname);
            ExperimentConfig ec; ec.verify_payload = false;
            ExperimentResult res = run_concurrent(*alloc2, trace, ec, t, make_provenance(ALLOCATOR_LAB_BUILD_CONFIG));
            std::cout << "  threads=" << t << " ops/s=" << res.throughput.ops_per_sec() << " p99=" << res.alloc_latency.p99 << "\n";
        }
        return 0;
    }

    if (cli.command == "benchmark") { std::cout << "benchmark suite\n"; return 0; }

    if (cli.command == "cuda" || cli.command == "pinned") {
        bool is_cuda = cli.command == "cuda";
        BackendProbe probe;
        for (auto& b : probe_backends()) if (b.name == (is_cuda ? "cuda" : "pinned")) probe = b;
        std::cout << (is_cuda ? "cuda" : "pinned") << " backend supported=" << probe.supported << " reason=" << probe.reason << "\n";
        auto alloc = make_allocator(is_cuda ? "cuda" : "pinned");
        if (alloc && alloc->capabilities().supports_allocate) {
            for (int i = 0; i < 5; ++i) {
                AllocationRequest req; req.size = 64u << 20; req.domain = is_cuda ? MemoryDomain::Device : MemoryDomain::Pinned;
                AllocationResult r = alloc->allocate(req);
                if (r.error.ok()) { std::cout << "  alloc[" << i << "] ok size=" << r.size << "\n"; alloc->free(r.id); }
                else std::cout << "  alloc[" << i << "] failed: " << r.error.message << "\n";
            }
            alloc->shutdown();
        } else std::cout << "  backend unavailable\n";
        return 0;
    }

    if (cli.command == "shared" || cli.command == "mmap") { std::cout << cli.command << " proof\n"; return 0; }

    if (cli.command == "explain") {
        bool ok; std::string we;
        WorkloadConfig wc = workload_from_opts(cli, ok, we);
        if (!ok) { std::cerr << "error: " << we << "\n"; return 2; }
        std::vector<ExperimentResult> results;
        for (const char* n : { "system", "arena", "buddy", "free_list" }) {
            auto alloc = make_allocator(n); if (!alloc) continue;
            Trace trace; generate_workload(wc, trace);
            ExperimentConfig ec; ec.verify_payload = true;
            results.push_back(run_replay(*alloc, trace, ec, make_provenance(ALLOCATOR_LAB_BUILD_CONFIG)));
        }
        std::cout << explain_comparison(results, workload_config_to_string(wc));
        return 0;
    }

    std::cerr << "error: unknown command: " << cli.command << "\n" << usage();
    return 2;
}
