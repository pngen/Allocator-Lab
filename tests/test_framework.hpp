#pragma once

// Allocator Lab 1.0.0 test framework (minimal, self-contained, no external deps).

#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace al_test {

using TestFn = void (*)();

struct CheckFailed { std::string expr; std::string file; int line; };

inline std::vector<std::pair<std::string, TestFn>>& test_registry() {
    static std::vector<std::pair<std::string, TestFn>> r;
    return r;
}

struct Registrar {
    Registrar(const char* name, TestFn fn) { test_registry().emplace_back(std::string(name), fn); }
};

inline int run_all() {
    std::string filter;
    {
        char* buf = nullptr; std::size_t len = 0;
        if (_dupenv_s(&buf, &len, "AL_TEST_FILTER") == 0 && buf) { filter.assign(buf); free(buf); }
    }
    int failed = 0; int ran = 0;
    for (auto& t : test_registry()) {
        if (!filter.empty() && std::string(t.first).find(filter) == std::string::npos) continue;
        ++ran;
        try { t.second(); std::cout << "[ OK ] " << t.first << "\n" << std::flush; }
        catch (const CheckFailed& cf) { std::cout << "[FAIL] " << t.first << " | " << cf.file << ":" << cf.line << " " << cf.expr << "\n" << std::flush; ++failed; }
        catch (const std::exception& ex) { std::cout << "[FAIL] " << t.first << " | unexpected exception: " << ex.what() << "\n" << std::flush; ++failed; }
        catch (...) { std::cout << "[FAIL] " << t.first << " | unexpected non-std exception\n" << std::flush; ++failed; }
    }
    std::cout << "\n" << ran - failed << "/" << ran << " tests passed\n" << std::flush;
    return failed;
}

} // namespace al_test

#define TEST(name) static void name(); static ::al_test::Registrar reg_##name(#name, name); static void name()
#define REQUIRE(cond) do { if (!(cond)) throw ::al_test::CheckFailed{ #cond, __FILE__, __LINE__ }; } while (0)
#define REQUIRE_EQ(a, b) do { if (!((a) == (b))) { std::ostringstream al_os_; al_os_ << #a " == " #b " (got " << (a) << " vs " << (b) << ")"; throw ::al_test::CheckFailed{ al_os_.str(), __FILE__, __LINE__ }; } } while (0)
#define CHECK(cond) REQUIRE(cond)
