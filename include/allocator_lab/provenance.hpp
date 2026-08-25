#pragma once

// Allocator Lab 1.0.0
// Experiment provenance: how/when/where a measurement was taken.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <string>

namespace allocator_lab {

/// Facts captured at build time (compile-time macros) and at runtime about the
/// environment a result was produced in. Only fields actually queried are
/// populated; nothing is invented.
struct BuildProvenance {
    std::string allocator_lab_version = "1.0.0";
    std::string git_revision;
    std::string os;
    std::string architecture;
    std::string compiler;
    std::string compiler_version;
    std::string build_config;        // Release / Debug
    std::string cmake_version;
    std::string cpu_model;
    std::uint32_t logical_cores = 0;
    std::uint32_t physical_cores = 0;
    std::uint64_t system_ram_bytes = 0;
    std::string backend;             // e.g. "cuda", "pinned", "shared", "host"
    std::string accelerator_name;    // e.g. "NVIDIA GeForce RTX 5090"
    std::string cuda_driver_version;
    std::string cuda_runtime_version;
    std::string cuda_toolkit;
    std::uint64_t seed = 0;
    std::string allocator_config;
    std::string workload_config;
    std::int64_t epoch_seconds = 0;  // wall-clock capture time (UTC seconds)
    std::string timestamp_iso;       // ISO-8601 UTC string
};

/// Fill the build-time and runtime-queryable portions of provenance. GPU / CUDA
/// fields are filled only when a backend is requested. See src/provenance.cpp.
BuildProvenance make_provenance(const std::string& build_config,
                                const std::string& backend = std::string{});

} // namespace allocator_lab
