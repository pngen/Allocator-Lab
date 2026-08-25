# Allocator Lab 1.0.0
# CMake build system.
#
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Summon Software Labs

cmake_minimum_required(VERSION 3.21)

project(AllocatorLab
    VERSION 1.0.0
    DESCRIPTION "Open-source, vendor-neutral laboratory for designing, benchmarking, stress-testing, and comparing memory allocators."
    HOMEPAGE_URL "https://github.com/pngen/Allocator-Lab"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(ALLOCATOR_LAB_BUILD_TESTS "Build the test suite" ON)
option(ALLOCATOR_LAB_BUILD_BENCHMARKS "Build the benchmark suite" ON)
option(ALLOCATOR_LAB_BUILD_EXAMPLES "Build the examples" ON)

# ---------------------------------------------------------------------------
# Warnings: zero warnings is a hard requirement. /W4 /WX on MSVC.
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /WX /EHsc /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------------------
# Optional CUDA backend. We only link the CUDA runtime library; no .cu files
# are compiled, so the CUDA language is not enabled here.
# ---------------------------------------------------------------------------
find_package(CUDAToolkit QUIET)
set(ALLOCATOR_LAB_HAS_CUDA OFF)
if(CUDAToolkit_FOUND)
    set(ALLOCATOR_LAB_HAS_CUDA ON)
endif()

# Git revision for provenance.
execute_process(COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    OUTPUT_VARIABLE ALLOCATOR_LAB_GIT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
set(ALLOCATOR_LAB_SOURCES
    src/workload.cpp
    src/trace.cpp
    src/experiment.cpp
    src/report.cpp
    src/provenance.cpp
    src/backend.cpp)

add_library(allocator_lab STATIC ${ALLOCATOR_LAB_SOURCES})
add_library(AllocatorLab::allocator_lab ALIAS allocator_lab)

target_include_directories(allocator_lab
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_definitions(allocator_lab
    PUBLIC
        ALLOCATOR_LAB_VERSION="1.0.0"
        "ALLOCATOR_LAB_BUILD_CONFIG=\"$<CONFIG>\"")

if(ALLOCATOR_LAB_GIT_REVISION)
    target_compile_definitions(allocator_lab PUBLIC
        ALLOCATOR_LAB_GIT_REVISION="${ALLOCATOR_LAB_GIT_REVISION}")
endif()

if(ALLOCATOR_LAB_HAS_CUDA)
    target_compile_definitions(allocator_lab PUBLIC ALLOCATOR_LAB_HAS_CUDA=1)
    target_include_directories(allocator_lab PUBLIC ${CUDAToolkit_INCLUDE_DIRS})
    target_link_directories(allocator_lab PUBLIC ${CUDAToolkit_LIBRARY_DIR})
    target_link_libraries(allocator_lab PUBLIC cudart)
endif()

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
add_executable(allocator-lab src/cli/main.cpp)
target_link_libraries(allocator-lab PRIVATE allocator_lab)

add_executable(allocator-lab-shm-creator src/shm_creator_main.cpp)
target_link_libraries(allocator-lab-shm-creator PRIVATE allocator_lab)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()
if(ALLOCATOR_LAB_BUILD_TESTS)
    add_executable(allocator-lab-tests
        tests/test_main.cpp
        tests/core_tests.cpp
        tests/allocator_tests.cpp
        tests/property_tests.cpp
        tests/concurrency_tests.cpp
        tests/trace_tests.cpp
        tests/adversarial_tests.cpp
        tests/shared_memory_proof.cpp)
    target_link_libraries(allocator-lab-tests PRIVATE allocator_lab)
    target_include_directories(allocator-lab-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    if(ALLOCATOR_LAB_HAS_CUDA)
        target_compile_definitions(allocator-lab-tests PRIVATE ALLOCATOR_LAB_TEST_HAS_CUDA=1)
    endif()
    add_test(NAME allocator_lab_tests COMMAND allocator-lab-tests)
endif()

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
if(ALLOCATOR_LAB_BUILD_BENCHMARKS)
    add_executable(allocator-lab-benchmarks benchmarks/benchmarks.cpp)
    target_link_libraries(allocator-lab-benchmarks PRIVATE allocator_lab)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
if(ALLOCATOR_LAB_BUILD_EXAMPLES)
    add_executable(allocator-lab-example-direct examples/direct_allocation.cpp)
    target_link_libraries(allocator-lab-example-direct PRIVATE allocator_lab)
    add_executable(allocator-lab-example-compare examples/compare.cpp)
    target_link_libraries(allocator-lab-example-compare PRIVATE allocator_lab)
    add_executable(allocator-lab-example-arena examples/arena.cpp)
    target_link_libraries(allocator-lab-example-arena PRIVATE allocator_lab)
    add_executable(allocator-lab-example-sizeclass examples/size_class.cpp)
    target_link_libraries(allocator-lab-example-sizeclass PRIVATE allocator_lab)
    add_executable(allocator-lab-example-fragment examples/fragmentation.cpp)
    target_link_libraries(allocator-lab-example-fragment PRIVATE allocator_lab)
    add_executable(allocator-lab-example-trace examples/trace_roundtrip.cpp)
    target_link_libraries(allocator-lab-example-trace PRIVATE allocator_lab)
    add_executable(allocator-lab-example-replay examples/replay.cpp)
    target_link_libraries(allocator-lab-example-replay PRIVATE allocator_lab)
    add_executable(allocator-lab-example-concurrent examples/concurrent.cpp)
    target_link_libraries(allocator-lab-example-concurrent PRIVATE allocator_lab)
    add_executable(allocator-lab-example-failure examples/failure_injection.cpp)
    target_link_libraries(allocator-lab-example-failure PRIVATE allocator_lab)
    add_executable(allocator-lab-example-report examples/report.cpp)
    target_link_libraries(allocator-lab-example-report PRIVATE allocator_lab)
    add_executable(allocator-lab-example-custom examples/custom_allocator.cpp)
    target_link_libraries(allocator-lab-example-custom PRIVATE allocator_lab)
    add_executable(allocator-lab-example-pinned examples/pinned.cpp)
    target_link_libraries(allocator-lab-example-pinned PRIVATE allocator_lab)
    add_executable(allocator-lab-example-cuda examples/cuda.cpp)
    target_link_libraries(allocator-lab-example-cuda PRIVATE allocator_lab)
    add_executable(allocator-lab-example-shared examples/shared_memory.cpp)
    target_link_libraries(allocator-lab-example-shared PRIVATE allocator_lab)
    add_executable(allocator-lab-example-mmap examples/mmap.cpp)
    target_link_libraries(allocator-lab-example-mmap PRIVATE allocator_lab)
endif()

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS allocator_lab allocator-lab
    EXPORT AllocatorLabTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

install(DIRECTORY include/allocator_lab
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(EXPORT AllocatorLabTargets
    FILE AllocatorLabTargets.cmake
    NAMESPACE AllocatorLab::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/allocator_lab)

include(CMakePackageConfigHelpers)
set(ALLOCATOR_LAB_CONFIG_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/allocator_lab)
configure_package_config_file(
    cmake/allocator_lab-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/allocator_lab-config.cmake
    INSTALL_DESTINATION ${ALLOCATOR_LAB_CONFIG_INSTALL_DIR})
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/allocator_lab-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/allocator_lab-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/allocator_lab-config-version.cmake
    DESTINATION ${ALLOCATOR_LAB_CONFIG_INSTALL_DIR})
