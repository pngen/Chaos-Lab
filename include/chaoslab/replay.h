#pragma once
// Replay modes: PLAN_REPLAY, EVIDENCE_REPLAY, COMPARE.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/campaign.h"
#include "chaoslab/evidence.h"
#include "chaoslab/scheduler.h"

#include <string>
#include <vector>

namespace chaoslab {

enum class ReplayMode { PLAN_REPLAY, EVIDENCE_REPLAY, COMPARE };
const char* replay_mode_name(ReplayMode m) noexcept;

struct ReplayReport {
  ReplayMode mode{ReplayMode::COMPARE};
  bool same_plan{true};
  bool same_injections{true};
  bool same_assertions{true};
  bool same_terminal_state{true};
  bool same_state_digest{true};
  std::vector<std::string> differences;

  bool all_same() const noexcept {
    return same_plan && same_injections && same_assertions && same_terminal_state && same_state_digest;
  }
  std::string summary() const;
};

/// Compare two evidence streams from two runs of the same campaign.
ReplayReport compare_runs(const std::vector<EvidenceRecord>& a,
                          const std::vector<EvidenceRecord>& b);

/// Re-evaluate an evidence-backed run: recompute the digest over the recorded
/// stream and compare it to the expected digest captured at run time.
ReplayReport replay_evidence(const std::vector<EvidenceRecord>& records,
                             const Digest256& expected_digest);

/// Reproduce the injection sequence of a campaign plan: plan the schedule from
/// seed and step it against a recorded event sequence. If no event sequence is
/// supplied it reconstructs the planned schedule only.
ReplayReport replay_plan(const ChaosCampaign& campaign,
                         const std::vector<std::string>& events,
                         const std::vector<std::string>& expected_injections);

} // namespace chaoslab
