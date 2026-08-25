#pragma once

// Allocator Lab 1.0.0
// Device / backend capability query surface.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <string>
#include <vector>

namespace allocator_lab {

/// A structured report about the accelerator / device available to the lab.
struct DeviceInfo {
    bool available = false;
    std::string name;
    std::uint64_t total_memory_bytes = 0;
    std::uint64_t free_memory_bytes = 0;
    std::string driver_version;
    std::string runtime_version;
    int device_count = 0;
    std::string backend;           // "cuda" / "pinned" (uses CUDA runtime)
    bool has_cuda_build = false;
    bool supports_cuda_malloc_async = false;
};

/// Query device info. backend is one of "cuda". Returns available=false and
/// has_cuda_build=false when built without CUDA.
DeviceInfo query_device_info(const std::string& backend);

/// Query a backend's capability availability with a structured result.
struct BackendProbe {
    std::string name;
    bool supported = false;
    std::string reason;
};

std::vector<BackendProbe> probe_backends();

} // namespace allocator_lab
