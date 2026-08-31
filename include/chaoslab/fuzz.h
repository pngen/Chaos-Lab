#pragma once
// Deterministic fixed-seed randomized campaign generation and reduction.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/campaign.h"
#include "chaoslab/random.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace chaoslab {

/// A bounded randomized campaign generator. Given the same seed it always
/// produces the same campaign, so it remains fully deterministic/replayable.
class CampaignFuzzer {
public:
  explicit CampaignFuzzer(std::uint64_t seed = default_seed());

  /// Generate a bounded campaign combining worker death, stale replay,
  /// duplicate messages, persistence mutation, cancellation, retry,
  /// coordinator restart, resource pressure and reconnect failures.
  ChaosCampaign generate(int max_workers = 3, int max_faults = 8);

  std::uint64_t seed() const noexcept { return seed_; }

private:
  FaultCategory pick_category();
  void add_random_worker(ChaosCampaign& c, int idx);
  void add_fault(ChaosCampaign& c, int idx);

  std::uint64_t seed_;
  SplitMix64 rng_;
};

/// Minimize a campaign while preserving the failure predicate. Tries to remove
/// faults, steps, processes and messages. The original is never mutated.
ChaosCampaign reduce_campaign(const ChaosCampaign& original,
                              const std::function<bool(const ChaosCampaign&)>& still_fails);

/// A per-seed run bookkeeping that records the seed, generated campaign and any
/// failing seed so the failure can be reproduced.
struct FuzzRun {
  std::uint64_t seed{0};
  ChaosCampaign campaign;
  bool failed{false};
  CampaignGeneration generation;
};

} // namespace chaoslab
