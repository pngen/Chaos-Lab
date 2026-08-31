#pragma once
// Bounded host resource pressure and reservation governance.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/campaign.h"
#include "chaoslab/result.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

/// Counts of observable owned resources for a campaign or subsystem.
struct ResourceCounts {
  std::uint64_t child_processes{0};
  std::uint64_t open_sockets{0};
  std::uint64_t host_bytes{0};
  std::uint64_t device_bytes{0};
  std::uint64_t temp_files{0};
  std::uint64_t threads{0};
};

/// Before / peak / after resource accounting and a leak verdict.
struct ResourceBaseline {
  ResourceCounts before;
  ResourceCounts peak;
  ResourceCounts after;
  bool leak{false};
  std::vector<std::string> notes;

  /// Deterministic multi-line delta report.
  std::string delta_report() const;
  /// One-line summary.
  std::string to_text() const;
};

/// Tracks observable resources across a campaign and produces a baseline/delta.
class ResourceBaselineTracker {
public:
  /// Observe a current snapshot. The first call records the baseline.
  void observe(const ResourceCounts& now);
  void note(std::string text) { baseline_.notes.push_back(std::move(text)); }

  const ResourceBaseline& baseline() const noexcept { return baseline_; }
  bool captured() const noexcept { return captured_; }
  bool leak() const noexcept { return baseline_.leak; }

  /// A convenience: the observed-after resource count.
  const ResourceCounts& after() const noexcept { return baseline_.after; }

private:
  ResourceBaseline baseline_;
  bool captured_{false};
};

} // namespace chaoslab
