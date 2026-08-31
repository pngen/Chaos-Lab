#pragma once
// Safety envelope: bounds every destructive action to campaign-owned scope.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/campaign.h"
#include "chaoslab/result.h"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace chaoslab {

/// Enforces the declared campaign resource envelope at runtime. No destructive
/// action may proceed unless it passes the corresponding guard.
class SafetyEnvelope {
public:
  explicit SafetyEnvelope(ResourceEnvelope env = {}) : env_(std::move(env)) {}

  const ResourceEnvelope& config() const noexcept { return env_; }

  /// Validate that an executable is allowed to be launched.
  Status allow_executable(const std::string& path) const;

  /// Validate that a target is owned by this campaign.
  Status allow_target(TargetId id) const;

  /// Validate that a path is within the allowed temp root (if one is set).
  Status allow_path(const std::string& path) const;

  /// Validate that a PID is in the allowed PID set (if non-empty).
  Status allow_pid(std::uint64_t pid) const;

  /// Reserve one child-process slot. Fails if the cap would be exceeded.
  Status acquire_child();
  void release_child() noexcept;
  int child_count() const noexcept { return child_count_; }
  int peak_child_count() const noexcept { return peak_child_count_; }
  bool can_spawn_more() const noexcept;

  /// Register an owned target id.
  void add_owned_target(TargetId id);
  bool is_owned_target(TargetId id) const noexcept;

  /// Track an owned socket; cap is enforced on acquire.
  Status acquire_socket();
  void release_socket() noexcept;
  int socket_count() const noexcept { return socket_count_; }
  int peak_socket_count() const noexcept { return peak_socket_count_; }

  /// Track a restart; cap enforced.
  Status acquire_restart();
  int restart_count() const noexcept { return restart_count_; }
  int restart_budget_remaining() const noexcept;

  /// Validate the envelope config structurally.
  Status validate() const;

private:
  ResourceEnvelope env_;
  std::set<TargetId> owned_;
  int child_count_{0};
  int socket_count_{0};
  int restart_count_{0};
  int peak_child_count_{0};
  int peak_socket_count_{0};
};

} // namespace chaoslab
