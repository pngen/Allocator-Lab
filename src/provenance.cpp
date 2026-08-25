// Allocator Lab 1.0.0
// Provenance capture: real OS/CPU/RAM/CUDA queries.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "allocator_lab/provenance.hpp"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sstream>
#include <vector>

#include "allocator_lab/detail/platform.hpp"

#ifdef ALLOCATOR_LAB_HAS_CUDA
  #include <cuda_runtime.h>
#endif

namespace allocator_lab {
namespace {

std::string ws_to_utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    int len = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &out[0], len, nullptr, nullptr);
    return out;
}

std::string os_version() {
    typedef LONG(WINAPI* RtlGetVersionT)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionT fn = ntdll ? reinterpret_cast<RtlGetVersionT>(GetProcAddress(ntdll, "RtlGetVersion")) : nullptr;
    std::ostringstream os;
    os << "Windows ";
    if (fn) {
        RTL_OSVERSIONINFOW oi{}; oi.dwOSVersionInfoSize = sizeof(oi);
        if (fn(&oi) == 0) { os << oi.dwMajorVersion << "." << oi.dwBuildNumber; return os.str(); }
    }
    os << "(unknown)";
    return os.str();
}

std::string cpu_model() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &key) == ERROR_SUCCESS) {
        wchar_t buf[512] = {0}; DWORD sz = sizeof(buf);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type, reinterpret_cast<LPBYTE>(buf), &sz) == ERROR_SUCCESS) {
            RegCloseKey(key);
            return ws_to_utf8(buf);
        }
        RegCloseKey(key);
    }
    return "unknown";
}

std::uint32_t physical_cores() {
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
    if (GetLogicalProcessorInformation(buf.data(), &len)) {
        std::uint32_t count = 0;
        for (const auto& info : buf) if (info.Relationship == RelationProcessorCore) ++count;
        return count;
    }
    return 0;
}

void fill_cuda(BuildProvenance& p, const std::string& backend) {
    if (backend.find("cuda") == std::string::npos && backend.find("pinned") == std::string::npos) return;
#ifdef ALLOCATOR_LAB_HAS_CUDA
    int count = 0;
    if (cudaGetDeviceCount(&count) == cudaSuccess && count > 0) {
        cudaDeviceProp prop{};
        if (cudaGetDeviceProperties(&prop, 0) == cudaSuccess) {
            p.accelerator_name = prop.name;
        }
        int driver = 0, runtime = 0;
        if (cudaDriverGetVersion(&driver) == cudaSuccess) p.cuda_driver_version = std::to_string(driver);
        if (cudaRuntimeGetVersion(&runtime) == cudaSuccess) p.cuda_runtime_version = std::to_string(runtime);
        p.backend = backend;
    }
#else
    p.backend = backend + "(no cuda build)";
#endif
}

} // namespace

BuildProvenance make_provenance(const std::string& build_config, const std::string& backend) {
    BuildProvenance p;
    p.allocator_lab_version = "1.0.0";
#ifdef ALLOCATOR_LAB_GIT_REVISION
    p.git_revision = ALLOCATOR_LAB_GIT_REVISION;
#endif
    p.os = os_version();
    p.cpu_model = cpu_model();
    SYSTEM_INFO si{}; GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: p.architecture = "x86_64"; break;
        case PROCESSOR_ARCHITECTURE_ARM64: p.architecture = "arm64"; break;
        case PROCESSOR_ARCHITECTURE_INTEL: p.architecture = "x86"; break;
        default: p.architecture = "unknown"; break;
    }
#ifdef ALLOCATOR_LAB_CMAKE_VERSION
    p.cmake_version = ALLOCATOR_LAB_CMAKE_VERSION;
#endif
#ifdef _MSC_VER
    p.compiler = "MSVC";
#ifdef _MSC_FULL_VER
    p.compiler_version = std::to_string(_MSC_FULL_VER);
#else
    p.compiler_version = std::to_string(_MSC_VER);
#endif
#elif defined(__GNUC__)
    p.compiler = "GCC"; p.compiler_version = __VERSION__;
#elif defined(__clang__)
    p.compiler = "Clang"; p.compiler_version = __clang_version__;
#endif
    p.build_config = build_config;
    p.logical_cores = si.dwNumberOfProcessors;
    p.physical_cores = physical_cores();
    MEMORYSTATUSEX ms{}; ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) p.system_ram_bytes = ms.ullTotalPhys;
    SYSTEMTIME st{}; GetSystemTime(&st);
    {
        FILETIME ft; SystemTimeToFileTime(&st, &ft);
        ULARGE_INTEGER ul; ul.LowPart = ft.dwLowDateTime; ul.HighPart = ft.dwHighDateTime;
        p.epoch_seconds = static_cast<std::int64_t>((ul.QuadPart / 10000000ULL) - 11644473600ULL);
    }
    char iso[32]; std::snprintf(iso, sizeof(iso), "%04u-%02u-%02uT%02u:%02u:%02uZ",
                                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    p.timestamp_iso = iso;
    fill_cuda(p, backend);
    return p;
}

} // namespace allocator_lab
