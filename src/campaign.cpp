#include "chaoslab/campaign.h"

#include "chaoslab/text.h"

#include <algorithm>
#include <array>
#include <limits>

namespace chaoslab {

namespace {
constexpr std::array<const char*, 11> kPhaseNames = {
  "setup","baseline","armed","injecting","observing","recovering","verifying","cleanup","complete","failed","aborted"
};
constexpr std::array<const char*, 8> kTargetKindNames = {
  "process","worker","coordinator","transport_proxy","persistence_file","resource","device"
};
constexpr std::array<const char*, 6> kCleanupNames = {
  "kill_process","close_socket","delete_file","rmdir","free_allocation","unspecified"
};

// Legal phase graph.
bool edge(CampaignPhase f, CampaignPhase t) noexcept {
  switch (f) {
    case CampaignPhase::SETUP:    return t == CampaignPhase::BASELINE || t == CampaignPhase::FAILED || t == CampaignPhase::ABORTED;
    case CampaignPhase::BASELINE: return t == CampaignPhase::ARMED || t == CampaignPhase::FAILED || t == CampaignPhase::ABORTED;
    case CampaignPhase::ARMED:    return t == CampaignPhase::INJECTING || t == CampaignPhase::FAILED || t == CampaignPhase::ABORTED;
    case CampaignPhase::INJECTING: return t == CampaignPhase::OBSERVING || t == CampaignPhase::RECOVERING || t == CampaignPhase::FAILED || t == CampaignPhase::ABORTED;
    case CampaignPhase::OBSERVING: return t == CampaignPhase::RECOVERING || t == CampaignPhase::VERIFYING || t == CampaignPhase::FAILED || t == CampaignPhase::ABORTED;
    case CampaignPhase::RECOVERING: return t == CampaignPhase::VERIFYING || t == CampaignPhase::FAILED || t == CampaignPhase::ABORTED;
    case CampaignPhase::VERIFYING: return t == CampaignPhase::CLEANUP || t == CampaignPhase::FAILED || t == CampaignPhase::ABORTED;
    case CampaignPhase::CLEANUP:  return t == CampaignPhase::COMPLETE || t == CampaignPhase::FAILED || t == CampaignPhase::ABORTED;
    case CampaignPhase::COMPLETE:
    case CampaignPhase::FAILED:
    case CampaignPhase::ABORTED:  return false; // terminal; cannot re-enter execution
  }
  return false;
}
} // namespace

const char* campaign_phase_name(CampaignPhase p) noexcept {
  auto i = static_cast<std::size_t>(p);
  return i < kPhaseNames.size() ? kPhaseNames[i] : "unknown";
}

bool can_transition(CampaignPhase from, CampaignPhase to) noexcept {
  return edge(from, to);
}

bool is_terminal(CampaignPhase p) noexcept {
  return p == CampaignPhase::COMPLETE || p == CampaignPhase::FAILED || p == CampaignPhase::ABORTED;
}

const char* target_kind_name(TargetKind k) noexcept {
  auto i = static_cast<std::size_t>(k);
  return i < kTargetKindNames.size() ? kTargetKindNames[i] : "unknown";
}

const char* cleanup_action_name(CleanupActionKind k) noexcept {
  auto i = static_cast<std::size_t>(k);
  return i < kCleanupNames.size() ? kCleanupNames[i] : "unknown";
}

bool ResourceEnvelope::validate() const noexcept {
  if (max_child_processes < 1) return false;
  if (max_restarts < 1) return false;
  if (max_open_sockets < 1) return false;
  if (!allowed_temp_root.empty() && allowed_temp_root.size() > 512) return false;
  return true;
}

bool ChaosCampaign::validate(std::string& error) const {
  if (!envelope.validate()) {
    error = "resource envelope invalid";
    return false;
  }
  if (seed == 0) { error = "deterministic seed must be non-zero"; return false; }
  if (repetition_count < 1) { error = "repetition_count must be >= 1"; return false; }

  // Targets must have unique ids.
  for (std::size_t i = 0; i < targets.size(); ++i) {
    for (std::size_t j = i + 1; j < targets.size(); ++j) {
      if (targets[i].id == targets[j].id) {
        error = "duplicate target id: " + targets[i].id.str();
        return false;
      }
    }
    auto& t = targets[i];
    if (t.kind == TargetKind::PROCESS && t.executable.empty()) {
      error = "process target '" + t.name + "' has no executable";
      return false;
    }
  }

  // Every fault must target an owned target.
  for (auto& f : fault_schedule) {
    for (auto& tid : f.targets) {
      bool owned = std::any_of(targets.begin(), targets.end(), [&](const TargetSpec& t) { return t.id == tid; });
      if (!owned) {
        error = "fault targets non-owned target: " + tid.str();
        return false;
      }
    }
  }
  return true;
}

} // namespace chaoslab
