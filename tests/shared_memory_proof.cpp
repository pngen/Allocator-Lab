// Allocator Lab 1.0.0 shared-memory two-process validation.
//
// This test launches a real separate OS process (allocator-lab-shm-creator) that
// creates a named shared mapping, allocates, writes a deterministic pattern, and
// publishes the block. This test process then opens the SAME mapping (a second
// OS process) and verifies the data is visible, checks stale/missing-object
// rejection, signals release, and confirms clean shutdown of the creator.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include "tests/test_framework.hpp"
#include "allocator_lab/allocators/shared_allocator.hpp"
#include <windows.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

using namespace allocator_lab;

static std::string exe_dir() {
    char buf[MAX_PATH] = {0};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    auto pos = p.find_last_of("\\/");
    return pos == std::string::npos ? std::string(".") : p.substr(0, pos);
}

TEST(shared_memory_two_process_proof) {
    // Unique name per run to avoid stale collisions.
    std::string name = "al_proof_" + std::to_string(GetCurrentProcessId()) + "_" + std::to_string(
        static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count() % 100000));
    const std::size_t size = 8u << 20;
    const std::uint64_t seed = 0xABCDEF1234567ULL;
    std::string ready = name + "_ready.txt";
    std::string release = name + "_release.txt";
    std::string meta = name + "_meta.txt";

    std::string creator = exe_dir() + "\\allocator-lab-shm-creator.exe";
    // Launch the creator process.
    std::string cmdline = "\"" + creator + "\" " + name + " " + std::to_string(size) + " " + std::to_string(seed);
    STARTUPINFOA si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessA(creator.c_str(), const_cast<char*>(cmdline.c_str()), nullptr, nullptr, FALSE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    if (!ok) { std::cout << "[SKIP] could not launch shm creator: " << GetLastError() << "\n"; return; }

    // Wait for the creator to write the ready + meta files.
    bool ready_found = false;
    for (int i = 0; i < 300 && !ready_found; ++i) {
        ready_found = std::filesystem::exists(ready) && std::filesystem::exists(meta);
        if (!ready_found) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    if (!ready_found) { std::cout << "DEBUG not ready; creator=" << creator << " cmd=" << cmdline << " name=" << name << "\n"; }
    REQUIRE(ready_found);

    // Read meta: offset size seed.
    std::uint64_t offset = 0, msize = 0, mseed = 0;
    { std::ifstream f(meta); std::string a, b, c; f >> a >> b >> c; offset = std::stoull(a); msize = std::stoull(b); mseed = std::stoull(c); }
    REQUIRE_EQ(mseed, seed);
    REQUIRE(msize > 0);

    // Second process: open the SAME named mapping and verify data visibility.
    SharedAllocator v(name, size, SharedAllocator::Mode::Open);
    REQUIRE(v.initialized());
    char* region = v.region();
    REQUIRE(region != nullptr);
    std::uint8_t* addr = reinterpret_cast<std::uint8_t*>(region + offset);
    std::uint64_t vs = seed ^ 0x9E3779B97F4A7C15ULL;
    bool all_match = true;
    for (std::size_t i = 0; i < static_cast<std::size_t>(msize); ++i) {
        vs = vs * 6364136223846793005ULL + 1442695040888963407ULL;
        if (addr[i] != static_cast<std::uint8_t>(vs >> 33)) { all_match = false; break; }
    }
    REQUIRE(all_match);
    v.shutdown();

    // Stale / missing object rejection: a name that was never created must fail.
    SharedAllocator bad(name + "_never_created", size, SharedAllocator::Mode::Open);
    REQUIRE(!bad.initialized());

    // Signal the creator to release and clean up.
    { std::ofstream f(release, std::ios::trunc); f << "1\n"; }
    DWORD wr = WaitForSingleObject(pi.hProcess, 60000);
    REQUIRE(wr == WAIT_OBJECT_0);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    REQUIRE_EQ(code, 0u);

    std::error_code ec;
    std::filesystem::remove(ready, ec);
    std::filesystem::remove(meta, ec);
    std::filesystem::remove(release, ec);
}
