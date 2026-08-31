#include "chaoslab/fuzz.h"

#include "chaoslab/text.h"

#include <algorithm>
#include <array>

namespace chaoslab {

namespace {
constexpr std::array<FaultCategory, 14> kPool = {
  FaultCategory::WORKER_DEATH,
  FaultCategory::COORDINATOR_DEATH,
  FaultCategory::STALE_EPOCH,
  FaultCategory::STALE_BOOT,
  FaultCategory::STALE_ATTEMPT,
  FaultCategory::STALE_GENERATION,
  FaultCategory::DUPLICATE_MESSAGE,
  FaultCategory::CONFLICTING_DUPLICATE,
  FaultCategory::PERSISTENCE_TRUNCATION,
  FaultCategory::PERSISTENCE_CORRUPTION,
  FaultCategory::RESOURCE_EXHAUSTION,
  FaultCategory::CONNECTION_DROP,
  FaultCategory::FRAME_CORRUPTION,
  FaultCategory::RECOVERY_FAILURE
};

FaultScope scope_for(FaultCategory c) {
  switch (c) {
    case FaultCategory::STALE_EPOCH: case FaultCategory::STALE_BOOT:
    case FaultCategory::STALE_ATTEMPT: case FaultCategory::STALE_GENERATION:
      return FaultScope::AUTHORITY;
    case FaultCategory::DUPLICATE_MESSAGE: case FaultCategory::CONFLICTING_DUPLICATE:
    case FaultCategory::CONNECTION_DROP: case FaultCategory::FRAME_CORRUPTION:
      return FaultScope::CONNECTION;
    case FaultCategory::PERSISTENCE_TRUNCATION: case FaultCategory::PERSISTENCE_CORRUPTION:
      return FaultScope::PERSISTENCE;
    case FaultCategory::RESOURCE_EXHAUSTION: return FaultScope::RESERVATION;
    case FaultCategory::RECOVERY_FAILURE: return FaultScope::PROCESS;
    default: return FaultScope::PROCESS;
  }
}
} // namespace

CampaignFuzzer::CampaignFuzzer(std::uint64_t seed) : seed_(seed), rng_(seed) {}

FaultCategory CampaignFuzzer::pick_category() {
  return kPool[rng_.below(kPool.size())];
}

void CampaignFuzzer::add_random_worker(ChaosCampaign& c, int idx) {
  TargetSpec t;
  t.id = TargetId((1ULL << 63) | static_cast<std::uint64_t>(idx + 1));
  t.kind = TargetKind::WORKER;
  t.name = "worker_" + std::to_string(idx);
  t.executable = "target_worker";
  t.owns_process = true;
  t.restart_budget = 2;
  c.targets.push_back(std::move(t));
}

// Ensure the coordinator target exists (workers reference a coordinator via faults).
void CampaignFuzzer::add_fault(ChaosCampaign& c, int idx) {
  FaultCategory cat = pick_category();
  TargetId target = TargetId((1ULL << 63) | static_cast<std::uint64_t>((idx % 3) + 1));
  // If no worker target exists yet, ensure at least one target.
  if (c.targets.empty()) add_random_worker(c, 0);
  FaultSpec f;
  f.id = FaultId(static_cast<std::uint64_t>(c.fault_schedule.size() + 1));
  f.category = cat;
  f.source = FaultSource::INJECTED;
  f.scope = scope_for(cat);
  f.severity = Severity::HIGH;
  f.timing = Timing::AT_EVENT;
  f.target = target;
  f.targets = {target};
  if (cat == FaultCategory::PERSISTENCE_TRUNCATION) f.params["offset"] = "16";
  if (cat == FaultCategory::RESOURCE_EXHAUSTION) f.params["percent"] = "90";
  c.fault_schedule.push_back(std::move(f));
}

ChaosCampaign CampaignFuzzer::generate(int max_workers, int max_faults) {
  ChaosCampaign c;
  c.id = CampaignId(seed_ ^ 0x5EED5EED5EED5EEDull);
  c.seed = seed_;
  c.generation = CampaignGeneration(1);
  c.purpose = "fuzzed campaign seed=" + std::to_string(seed_);

  int workers = static_cast<int>(1 + rng_.below(static_cast<std::uint64_t>(max_workers)));
  for (int i = 0; i < workers; ++i) add_random_worker(c, i);

  // Ensure a coordinator target exists.
  TargetSpec coord;
  coord.id = TargetId((1ULL << 63) | 0x8001ull);
  coord.kind = TargetKind::COORDINATOR;
  coord.name = "coordinator";
  coord.executable = "target_coordinator";
  coord.owns_process = true;
  c.targets.push_back(std::move(coord));

  int faults = static_cast<int>(1 + rng_.below(static_cast<std::uint64_t>(max_faults)));
  for (int i = 0; i < faults; ++i) add_fault(c, i);

  // Preconditions: coordinator + workers registered.
  Precondition pc;
  pc.name = "registered";
  pc.description = "all targets registered";
  pc.target = coord.id;
  pc.state_name = "registered";
  c.preconditions.push_back(pc);

  // Cleanup: kill processes for all owned process targets.
  for (auto& t : c.targets) {
    if (t.owns_process) {
      CleanupAction ca;
      ca.kind = CleanupActionKind::KILL_PROCESS;
      ca.target = t.id;
      c.cleanup_actions.push_back(std::move(ca));
    }
  }

  // Assertion: exactly one authority once recovered.
  AssertionSpec a;
  a.id = AssertionId(1);
  a.kind = AssertionKind::ASSERT_EXACTLY_ONE_AUTHORITY;
  c.assertions.push_back(std::move(a));

  // Envelope.
  c.envelope.max_child_processes = workers + 2;
  c.envelope.max_restarts = 8;
  c.envelope.allowed_executables = { "target_worker", "target_coordinator" };
  return c;
}

ChaosCampaign reduce_campaign(const ChaosCampaign& original,
                              const std::function<bool(const ChaosCampaign&)>& still_fails) {
  ChaosCampaign reduced = original;

  // Remove faults one at a time, greedily.
  for (std::size_t i = 0; i < reduced.fault_schedule.size();) {
    ChaosCampaign trial = reduced;
    trial.fault_schedule.erase(trial.fault_schedule.begin() + static_cast<std::ptrdiff_t>(i));
    if (still_fails(trial)) {
      reduced = std::move(trial);
      // Don't advance: erase shifted the tail down.
      continue;
    }
    ++i;
  }

  // Remove worker targets that are not referenced by any remaining fault.
  for (std::size_t i = 0; i < reduced.targets.size();) {
    bool referenced = false;
    for (auto& f : reduced.fault_schedule) {
      for (auto& tid : f.targets) if (tid == reduced.targets[i].id) referenced = true;
    }
    if (!referenced && reduced.targets[i].kind != TargetKind::COORDINATOR) {
      ChaosCampaign trial = reduced;
      trial.targets.erase(trial.targets.begin() + static_cast<std::ptrdiff_t>(i));
      if (still_fails(trial)) {
        reduced = std::move(trial);
        continue;
      }
    }
    ++i;
  }

  return reduced;
}

} // namespace chaoslab
