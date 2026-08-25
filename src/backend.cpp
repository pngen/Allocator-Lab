// Allocator Lab 1.0.0
// Device / backend property implementation.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "allocator_lab/backend.hpp"

#ifdef ALLOCATOR_LAB_HAS_CUDA
  #include <cuda_runtime.h>
#endif

namespace allocator_lab {

DeviceInfo query_device_info(const std::string& backend) {
    DeviceInfo info;
    info.backend = backend;
    info.has_cuda_build = false;
#ifdef ALLOCATOR_LAB_HAS_CUDA
    info.has_cuda_build = true;
    int count = 0;
    if (cudaGetDeviceCount(&count) == cudaSuccess) {
        info.device_count = count;
        info.available = count > 0;
        if (count > 0) {
            cudaDeviceProp prop{};
            if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess) {
                info.name = prop.name;
                info.total_memory_bytes = static_cast<std::uint64_t>(prop.totalGlobalMem);
            }
            std::size_t free = 0, total = 0;
            if (cudaMemGetInfo(&free, &total) == cudaSuccess) {
                info.free_memory_bytes = static_cast<std::uint64_t>(free);
                if (info.total_memory_bytes == 0) info.total_memory_bytes = static_cast<std::uint64_t>(total);
            }
            int driver = 0, runtime = 0;
            if (cudaDriverGetVersion(&driver) == cudaSuccess) info.driver_version = std::to_string(driver);
            if (cudaRuntimeGetVersion(&runtime) == cudaSuccess) info.runtime_version = std::to_string(runtime);
            int major = prop.major, minor = prop.minor;
            bool has_async = (prop.major >= 0);
            /* cudaMallocAsync available in runtime >= 11.2 */
            int rt = 0; if (cudaRuntimeGetVersion(&rt) == cudaSuccess) info.supports_cuda_malloc_async = rt >= 11020;
            (void)major; (void)minor; (void)has_async;
        }
    }
#else
    info.available = false;
#endif
    return info;
}

std::vector<BackendProbe> probe_backends() {
    std::vector<BackendProbe> out;
    out.push_back({ "host", true, "system malloc/free" });
    out.push_back({ "aligned", true, "aligned system allocation" });
    out.push_back({ "pinned", false, "cudaMallocHost; requires CUDA runtime" });
    out.push_back({ "cuda", false, "cudaMalloc; requires CUDA runtime" });
    out.push_back({ "cuda_pool", false, "cudaMallocAsync; requires CUDA >= 11.2" });
    out.push_back({ "shared", false, "Windows file mapping; requires Windows" });
    out.push_back({ "mapped", false, "file-backed mapping; requires Windows" });
#ifdef ALLOCATOR_LAB_HAS_CUDA
    DeviceInfo d = query_device_info("cuda");
    if (d.available) { out[2].supported = true; out[2].reason = d.name; out[3].supported = true; out[3].reason = d.name; }
    if (d.supports_cuda_malloc_async) { out[4].supported = true; out[4].reason = "cudaMallocAsync supported"; }
#endif
#if defined(_WIN32) || defined(_WIN64)
    out[5].supported = true; out[5].reason = "Windows file mapping available";
    out[6].supported = true; out[6].reason = "file-backed mapping available";
#endif
    return out;
}

} // namespace allocator_lab
