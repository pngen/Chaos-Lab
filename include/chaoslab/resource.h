#pragma once
// Bounded host resource pressure and reservation governance.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/campaign.h"
#include "chaoslab/result.h"

#include <cstdint>
#include <map>
#include <string>

namespace chaoslab {

/// An RAII host memory allocation subject to the campaign envelope.
class HostAllocation {
public:
  HostAllocation() = default;
  ~HostAllocation();
  HostAllocation(const HostAllocation&) = delete;
  HostAllocation& operator=(const HostAllocation&) = delete;
  HostAllocation(HostAllocation&& other) noexcept;
  HostAllocation& operator=(HostAllocation&& other) noexcept;

  /// Allocate p bytes of host memory (bounded by p max). Returns status.
  Status allocate(std::size_t bytes, std::uint64_t max_bytes);
  void release() noexcept;
  std::size_t size() const noexcept { return bytes_; }
  const void* data() const noexcept { return ptr_; }
  void* data() noexcept { return ptr_; }

private:
  void* ptr_ = nullptr;
  std::size_t bytes_ = 0;
};

/// Governs host memory allocations and logical reservations against the
/// campaign envelope. This is a pure controller; it does not touch devices.
class ResourceGovernor {
public:
  explicit ResourceGovernor(ResourceEnvelope env = {}) : env_(std::move(env)) {}

  Status try_allocate(std::size_t bytes, std::size_t& allocated);
  void account_free(std::size_t bytes) noexcept;

  Status reserve(std::uint64_t units);
  void release_reservation(std::uint64_t units) noexcept;

  std::uint64_t allocated_bytes() const noexcept { return allocated_; }
  std::uint64_t reserved_units() const noexcept { return reserved_; }
  std::uint64_t host_cap_bytes() const noexcept { return env_.max_host_allocation_bytes; }

private:
  ResourceEnvelope env_;
  std::uint64_t allocated_{0};
  std::uint64_t reserved_{0};
};

/// Simulate host allocation pressure by driving allocations up to a governed
/// target. Returns a container of live allocations so callers can release them.
Status apply_host_pressure(std::uint64_t target_bytes, std::uint64_t cap_bytes,
                           std::vector<HostAllocation>& out, std::uint64_t& total);

} // namespace chaoslab
