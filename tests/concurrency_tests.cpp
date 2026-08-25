// Allocator Lab 1.0.0 concurrency and shutdown tests.
#include "tests/test_framework.hpp"
#include "tests/test_helpers.hpp"
#include "allocator_lab/detail/rng.hpp"
#include "allocator_lab/experiment.hpp"
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

using namespace allocator_lab;
using namespace al_test_helpers;

TEST(concurrent_alloc_free_threads) {
    SystemAllocator a;
    const int kThreads = 8;
    const int kOps = 20000;
    std::atomic<std::uint64_t> failures{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t) {
        threads.emplace_back([&, t]() {
            detail::SplitMix64 rng(1000 + t);
            std::vector<AllocationId> live;
            for (int i = 0; i < kOps; ++i) {
                std::uint64_t r = rng.next(100);
                if (r < 50 || live.empty()) {
                    AllocationRequest req; req.size = 1 + static_cast<std::size_t>(rng.next(4096));
                    AllocationResult res = a.allocate(req);
                    if (res.error.ok()) live.push_back(res.id);
                    else ++failures;
                } else {
                    std::size_t idx = static_cast<std::size_t>(rng.next(live.size()));
                    if (!a.free(live[idx]).ok()) ++failures;
                    for (std::size_t j = idx + 1; j < live.size(); ++j) live[j-1] = live[j];
                    live.pop_back();
                }
            }
            for (auto id : live) if (!a.free(id).ok()) ++failures;
        });
    }
    for (auto& w : threads) w.join();
    REQUIRE_EQ(failures.load(), 0u);
    AllocatorStatistics st = a.statistics();
    REQUIRE(st.live.live_count == 0);
}

TEST(concurrent_workload_runs_clean) {
    WorkloadConfig wc; wc.operations = 60000; wc.live_target = 2048; wc.threads = 8; wc.seed = 99;
    Trace trace; REQUIRE(generate_workload(wc, trace).ok());
    for (std::uint32_t t : { (std::uint32_t)1, (std::uint32_t)2, (std::uint32_t)4, (std::uint32_t)8 }) {
        SystemAllocator a;
        ExperimentConfig ec; ec.verify_payload = false;
        ExperimentResult res = run_concurrent(a, trace, ec, t, BuildProvenance{});
        REQUIRE(res.failure_count == 0);
        REQUIRE(a.statistics().live.live_count == 0);
    }
}

TEST(concurrent_shutdown_orderly) {
    SystemAllocator a;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> errors{0};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t]() {
            detail::SplitMix64 rng(700 + t);
            std::vector<AllocationId> live;
            while (!stop.load()) {
                std::uint64_t r = rng.next(100);
                if (r < 50 || live.empty()) {
                    AllocationRequest req; req.size = 1 + static_cast<std::size_t>(rng.next(1024));
                    AllocationResult res = a.allocate(req);
                    if (res.error.ok()) live.push_back(res.id);
                    else ++errors;
                } else {
                    std::size_t idx = static_cast<std::size_t>(rng.next(live.size()));
                    if (!a.free(live[idx]).ok()) ++errors;
                    for (std::size_t j = idx + 1; j < live.size(); ++j) live[j-1] = live[j];
                    live.pop_back();
                }
            }
            // drain before thread exits
            for (auto id : live) if (!a.free(id).ok()) ++errors;
        });
    }
    // let it run briefly then stop cleanly
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    stop.store(true);
    for (auto& w : threads) w.join();
    REQUIRE_EQ(errors.load(), 0u);
    REQUIRE(a.statistics().live.live_count == 0);
    a.shutdown();
    REQUIRE(a.statistics().live.live_count == 0);
}
