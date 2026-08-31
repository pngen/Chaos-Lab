#pragma once
// Authority envelope and stale-authority adversary model.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/identity.h"
#include "chaoslab/result.h"

#include <string>
#include <string_view>

namespace chaoslab {

/// The full set of authority dimensions carried by a message/operation. Each
/// component is a monotonically advancing generation. Chaotic campaigns replay
/// older values and prove the target rejects them.
struct AuthorityEnvelope {
  CoordinatorEpoch epoch;
  WorkerId worker;
  WorkerBootId boot;
  AttemptId attempt;
  AttemptGeneration attempt_gen;
  DispatchId dispatch;
  TargetGeneration target_gen;

  /// Deterministic "k=v;k=v;..." encoding.
  std::string to_text() const;

  /// Parse the text form back. Returns status; exact round-trip guaranteed.
  static Status parse(std::string_view text, AuthorityEnvelope& out);

  bool operator==(const AuthorityEnvelope&) const = default;
  bool operator!=(const AuthorityEnvelope&) const = default;
};

/// True when the candidate exactly matches the current authority.
bool authority_matches(const AuthorityEnvelope& current, const AuthorityEnvelope& candidate);

/// True when at least one dimension of the candidate is strictly older than the
/// current authority (i.e. the candidate is stale).
bool authority_is_stale(const AuthorityEnvelope& current, const AuthorityEnvelope& candidate);

/// Which dimension was stale (for explainability). Returns empty if not stale.
std::string stale_dimension(const AuthorityEnvelope& current, const AuthorityEnvelope& candidate);

/// Deliberate stale-combination matrix:
///   0 = fresh_epoch+stale_boot, 1 = fresh_boot+stale_attempt,
///   2 = current_attempt+stale_dispatch, 3 = stale_generation with valid payload,
///   4 = duplicate_current_completion, 5 = conflicting_duplicate_completion.
AuthorityEnvelope stale_combination(int kind, const AuthorityEnvelope& current);

/// Empty/default envelope with all dimensions zero.
AuthorityEnvelope default_authority();

} // namespace chaoslab
