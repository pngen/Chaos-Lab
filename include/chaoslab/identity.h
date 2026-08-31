#pragma once
// Strongly typed identity model for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace chaoslab {

/// Category of a strongly-typed identity.
enum class IdKind {
  Campaign, Scenario, Fault, Injection, Assertion, Evidence, Target,
  Process, Worker, WorkerBoot, Node, Device, Request, Operation,
  Attempt, Dispatch, Recovery, CoordinatorEpoch, AttemptGeneration,
  TargetGeneration, CampaignGeneration,
  Last
};

/// Human-readable name for an IdKind (stable, lowercase).
const char* id_kind_name(IdKind kind) noexcept;

/// A strongly-typed identifier over a 64-bit value.
/// Serializes deterministically as lowercase hex and round-trips exactly.
template <IdKind Kind>
class Id {
public:
  constexpr Id() noexcept = default;
  constexpr explicit Id(std::uint64_t value) noexcept : value_(value) {}

  static Id from_value(std::uint64_t value) noexcept { return Id(value); }

  std::uint64_t value() const noexcept { return value_; }

  /// Kind tag (compile-time).
  static constexpr IdKind kind() noexcept { return Kind; }

  /// Deterministic lowercase hex (always 16 characters, zero-padded).
  std::string to_string() const noexcept;

  /// Full stable form "kind:hex" for human-readable logs.
  std::string str() const noexcept;

  /// Parse either bare hex or the "kind:hex" form. Returns false on failure.
  static bool parse(std::string_view s, Id& out) noexcept;

  friend bool operator==(const Id& a, const Id& b) noexcept { return a.value_ == b.value_; }
  friend bool operator!=(const Id& a, const Id& b) noexcept { return a.value_ != b.value_; }
  friend bool operator<(const Id& a, const Id& b) noexcept { return a.value_ < b.value_; }

  struct Hash {
    std::size_t operator()(const Id& id) const noexcept {
      return std::hash<std::uint64_t>{}(id.value_);
    }
  };

private:
  std::uint64_t value_{0};
};

using CampaignId     = Id<IdKind::Campaign>;
using ScenarioId     = Id<IdKind::Scenario>;
using FaultId        = Id<IdKind::Fault>;
using InjectionId    = Id<IdKind::Injection>;
using AssertionId    = Id<IdKind::Assertion>;
using EvidenceId     = Id<IdKind::Evidence>;
using TargetId       = Id<IdKind::Target>;
using ProcessId      = Id<IdKind::Process>;
using WorkerId       = Id<IdKind::Worker>;
using WorkerBootId   = Id<IdKind::WorkerBoot>;
using NodeId         = Id<IdKind::Node>;
using DeviceId       = Id<IdKind::Device>;
using RequestId      = Id<IdKind::Request>;
using OperationId    = Id<IdKind::Operation>;
using AttemptId      = Id<IdKind::Attempt>;
using DispatchId     = Id<IdKind::Dispatch>;
using RecoveryId     = Id<IdKind::Recovery>;
using CoordinatorEpoch = Id<IdKind::CoordinatorEpoch>;
using AttemptGeneration = Id<IdKind::AttemptGeneration>;
using TargetGeneration  = Id<IdKind::TargetGeneration>;
using CampaignGeneration = Id<IdKind::CampaignGeneration>;

} // namespace chaoslab
