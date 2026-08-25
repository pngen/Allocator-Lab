#pragma once

// Allocator Lab 1.0.0
// Structured error handling.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <cstdint>
#include <string>
#include <utility>

namespace allocator_lab {

/// Stable machine-readable error codes. These are part of the versioned trace /
/// report surface, so their numeric values are fixed.
enum class ErrorCode : std::uint16_t {
    none = 0,
    invalid_argument = 1,
    size_too_large = 2,
    invalid_alignment = 3,
    alignment_unsupported = 4,
    out_of_memory = 5,
    capacity_exceeded = 6,
    not_supported = 7,
    invalid_handle = 8,
    foreign_handle = 9,
    double_free = 10,
    stale_handle = 11,
    already_live = 12,
    not_found = 13,
    operation_failed = 14,
    init_failed = 15,
    no_backend = 16,
    interface_error = 17,
    overflow = 18,
    trace_malformed = 19,
    trace_rejected = 20,
    underflow = 21,
    alignment_mismatch = 22,
    concurrency_violation = 23,
    invalid_config = 24,
    zero_size_invalid = 25,
    payload_integrity = 26,
};

typedef int ErrorCodeInt;

/// Short, stable name for an error code. Never returns nullptr.
inline const char* error_code_name(ErrorCode code) noexcept {
    switch (code) {
        case ErrorCode::none: return "none";
        case ErrorCode::invalid_argument: return "invalid_argument";
        case ErrorCode::size_too_large: return "size_too_large";
        case ErrorCode::invalid_alignment: return "invalid_alignment";
        case ErrorCode::alignment_unsupported: return "alignment_unsupported";
        case ErrorCode::out_of_memory: return "out_of_memory";
        case ErrorCode::capacity_exceeded: return "capacity_exceeded";
        case ErrorCode::not_supported: return "not_supported";
        case ErrorCode::invalid_handle: return "invalid_handle";
        case ErrorCode::foreign_handle: return "foreign_handle";
        case ErrorCode::double_free: return "double_free";
        case ErrorCode::stale_handle: return "stale_handle";
        case ErrorCode::already_live: return "already_live";
        case ErrorCode::not_found: return "not_found";
        case ErrorCode::operation_failed: return "operation_failed";
        case ErrorCode::init_failed: return "init_failed";
        case ErrorCode::no_backend: return "no_backend";
        case ErrorCode::interface_error: return "interface_error";
        case ErrorCode::overflow: return "overflow";
        case ErrorCode::trace_malformed: return "trace_malformed";
        case ErrorCode::trace_rejected: return "trace_rejected";
        case ErrorCode::underflow: return "underflow";
        case ErrorCode::alignment_mismatch: return "alignment_mismatch";
        case ErrorCode::concurrency_violation: return "concurrency_violation";
        case ErrorCode::invalid_config: return "invalid_config";
        case ErrorCode::zero_size_invalid: return "zero_size_invalid";
        case ErrorCode::payload_integrity: return "payload_integrity";
    }
    return "unknown";
}

/// A structured error carrying a code plus human-readable detail and context.
/// It is cheap and copyable. An empty error (code == none) represents success.
struct Error {
    ErrorCode code = ErrorCode::none;
    std::string message;
    std::string context;

    constexpr bool ok() const noexcept { return code == ErrorCode::none; }
    constexpr explicit operator bool() const noexcept { return !ok(); }
    const char* code_name() const noexcept { return error_code_name(code); }

    bool operator==(const Error& o) const noexcept { return code == o.code; }
    bool operator!=(const Error& o) const noexcept { return code != o.code; }
};

/// Construct an error value.
inline Error make_error(ErrorCode code, std::string message = {}, std::string context = {}) {
    return Error{code, std::move(message), std::move(context)};
}

} // namespace allocator_lab
