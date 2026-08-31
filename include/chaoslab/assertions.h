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
};

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
