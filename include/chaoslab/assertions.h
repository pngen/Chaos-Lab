#pragma once
// Typed assertion engine for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/identity.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace chaoslab {

enum class AssertionKind {
  ASSERT_ACCEPTED,
  ASSERT_REJECTED,
  ASSERT_STATE,
  ASSERT_TERMINAL,
  ASSERT_NOT_TERMINAL,
  ASSERT_EXACTLY_ONE_AUTHORITY,
  ASSERT_NO_STALE_MUTATION,
  ASSERT_NO_DOUBLE_COMMIT,
  ASSERT_NO_LEAK,
  ASSERT_ACCOUNTING_ZERO,
  ASSERT_DIGEST_EQUAL,
  ASSERT_DIGEST_DIFFERENT,
  ASSERT_RECOVERY_COMPLETE,
  ASSERT_PROCESS_EXIT,
  ASSERT_PROCESS_ALIVE,
  ASSERT_RESOURCE_BASELINE,
  Last
};

enum class AssertionStatus { UNEVALUATED, PASSED, FAILED, SKIPPED };

const char* assertion_kind_name(AssertionKind k) noexcept;
const char* assertion_status_name(AssertionStatus s) noexcept;

bool parse_assertion_kind(std::string_view s, AssertionKind& out) noexcept;

/// A declarative assertion embedded in a campaign.
struct AssertionSpec {
  AssertionId id;
  AssertionKind kind{AssertionKind::ASSERT_STATE};
  std::string description;
  std::map<std::string, std::string> params;
  bool required{true};

  void set_param(std::string key, std::string value);
  const std::string* param(std::string_view key) const;
};

struct AssertionResult;

/// Facts a campaign provides to the assertion evaluator. Every ASSERT_* kind
/// reads exactly the fields it needs; the rest are ignored, so a RunFacts value
/// can be built once and reused for a whole campaign phase.
struct RunFacts {
  bool accepted{false};
  bool rejected{false};
  std::string state;
  std::string expected_state;
  int authority_count{0};
  bool stale_mutation{false};
  bool double_commit{false};
  bool leak{false};
  bool accounting_zero{true};
  std::uint64_t accounting{0};
  std::string digest_actual;
  std::string digest_expected;
  bool recovery_complete{false};
  bool process_exited{false};
  int process_exit_code{0};
  bool process_alive{false};
  std::uint64_t resource_after{0};
  std::uint64_t resource_baseline{0};
  bool resource_baseline_ok{true};
  bool terminal{false};
};

/// Evaluate an assertion declaration deterministically against a set of facts.
/// Returns an AssertionResult with expected/observed and a pass/fail status.
AssertionResult evaluate_assertion(const AssertionSpec& spec, const RunFacts& facts,
                                   std::string phase = {});

/// The result of evaluating one assertion.
struct AssertionResult {
  AssertionId id;
  AssertionKind kind{AssertionKind::ASSERT_STATE};
  std::string expected;
  std::string observed;
  TargetId target;
  std::string phase;
  std::vector<EvidenceId> supporting_evidence;
  AssertionStatus status{AssertionStatus::UNEVALUATED};
  std::string explanation;

  bool passed() const noexcept { return status == AssertionStatus::PASSED; }
};

} // namespace chaoslab
