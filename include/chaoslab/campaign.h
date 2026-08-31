#pragma once
// Canonical campaign model for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/assertions.h"
#include "chaoslab/fault.h"
#include "chaoslab/identity.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace chaoslab {

/// Named campaign phases with guarded transitions.
enum class CampaignPhase {
  SETUP,
  BASELINE,
  ARMED,
  INJECTING,
  OBSERVING,
  RECOVERING,
  VERIFYING,
  CLEANUP,
  COMPLETE,
  FAILED,
  ABORTED
};

const char* campaign_phase_name(CampaignPhase p) noexcept;

/// Whether a transition from -> to is legal. All other edges are prohibited.
bool can_transition(CampaignPhase from, CampaignPhase to) noexcept;

/// Whether the phase represents a terminal state (COMPLETE / FAILED / ABORTED).
bool is_terminal(CampaignPhase p) noexcept;

enum class TargetKind { PROCESS, WORKER, COORDINATOR, TRANSPORT_PROXY, PERSISTENCE_FILE, RESOURCE, DEVICE };
const char* target_kind_name(TargetKind k) noexcept;

/// A target the campaign may own and act against.
struct TargetSpec {
  TargetId id;
  TargetKind kind{TargetKind::PROCESS};
  std::string name;
  std::string executable;
  std::vector<std::string> arguments;
  std::map<std::string, std::string> environment;
  std::string temp_dir;
  bool owns_process{false};
  int restart_budget{0};
};

/// A precondition that must hold before the fault schedule may begin.
struct Precondition {
  std::string name;
  std::string description;
  TargetId target;
  std::string state_name;  ///< e.g. "registered"
  bool operator==(const Precondition&) const = default;
};

enum class CleanupActionKind { KILL_PROCESS, CLOSE_SOCKET, DELETE_FILE, RMDIR, FREE_ALLOCATION, UNSPECIFIED };
const char* cleanup_action_name(CleanupActionKind k) noexcept;

struct CleanupAction {
  CleanupActionKind kind{CleanupActionKind::UNSPECIFIED};
  TargetId target;
  std::string payload;
  bool optional{false};
  bool operator==(const CleanupAction&) const = default;
};

/// The declared resource envelope that bounds every destructive action.
struct ResourceEnvelope {
  int max_child_processes{32};
  std::uint64_t max_host_allocation_bytes{0};   ///< 0 means validate-only (no allocation faulting)
  std::uint64_t max_device_allocation_bytes{0}; ///< 0 means governed/validate-only
  std::uint64_t max_temp_disk_bytes{512ULL * 1024 * 1024};
  int max_open_sockets{64};
  int max_restarts{128};
  std::vector<std::string> allowed_executables;
  std::string allowed_temp_root;
  std::vector<std::uint64_t> allowed_pids;

  bool validate() const noexcept;
  int max_restarts_effective() const noexcept { return max_restarts > 0 ? max_restarts : 1; }
};

enum class EvidencePolicy { RECORD_VERBOSE, RECORD_CONDENSED, RECORD_NONE };
struct ReplayPolicy {
  bool allow_plan_replay{true};
  bool allow_evidence_replay{true};
  bool allow_compare{true};
};

/// The canonical, immutable campaign definition. Once a campaign is started
/// its definition is copied into a CampaignRun and never mutated.
struct ChaosCampaign {
  CampaignId id;
  std::uint64_t seed{0};
  CampaignGeneration generation;
  std::string purpose;
  std::vector<TargetSpec> targets;
  std::vector<Precondition> preconditions;
  std::vector<FaultSpec> fault_schedule;   ///< ordered fault schedule
  std::vector<AssertionSpec> assertions;
  std::vector<CleanupAction> cleanup_actions;
  ResourceEnvelope envelope;
  EvidencePolicy evidence_policy{EvidencePolicy::RECORD_VERBOSE};
  ReplayPolicy replay_policy;
  int repetition_count{1};

  /// Validate structural invariants. Returns false with a message describing
  /// the first violation.
  bool validate(std::string& error) const;
};

} // namespace chaoslab
