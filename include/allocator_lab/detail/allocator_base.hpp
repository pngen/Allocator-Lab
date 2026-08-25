#pragma once

// Allocator Lab 1.0.0
// Common base for built-in allocator strategies.
//
// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Summon Software Labs

#include <mutex>
#include <string>

#include "allocator_lab/allocator.hpp"

namespace allocator_lab::detail {

class AllocatorBase : public AllocatorStrategy {
public:
    AllocatorBase(AllocatorId uid, std::string name, AllocatorKind kind)
        : id_(uid), name_(std::move(name)), kind_(kind) {}

    AllocatorId id() const noexcept override { return id_; }
    void set_id(AllocatorId id) noexcept override { id_ = id; }
    const std::string& name() const noexcept override { return name_; }
    AllocatorKind kind() const noexcept override { return kind_; }
    const AllocatorCapabilities& capabilities() const noexcept override { return caps_; }
    std::string describe() const override { return name_; }

protected:
    void set_capabilities(AllocatorCapabilities c) { caps_ = std::move(c); }

    AllocatorId id_ = 0;
    std::string name_;
    AllocatorKind kind_ = AllocatorKind::System;
    AllocatorCapabilities caps_;
};

} // namespace allocator_lab::detail
