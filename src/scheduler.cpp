#include "chaoslab/scheduler.h"

#include "chaoslab/text.h"

#include <algorithm>

namespace chaoslab {

const char* trigger_kind_name(TriggerKind k) noexcept {
  switch (k) {
    case TriggerKind::EVENT_COUNT: return "event_count";
    case TriggerKind::STATE_TRANSITION: return "state_transition";
    case TriggerKind::PROTOCOL_MESSAGE: return "protocol_message";
    case TriggerKind::OPERATION_PHASE: return "operation_phase";
    case TriggerKind::PROCESS_REGISTRATION: return "process_registration";
    case TriggerKind::DISPATCH: return "dispatch";
    case TriggerKind::COMPLETION_BOUNDARY: return "completion_boundary";
    case TriggerKind::PERSISTENCE_PHASE: return "persistence_phase";
    case TriggerKind::EXPLICIT_BARRIER: return "explicit_barrier";
    case TriggerKind::RESOURCE_THRESHOLD: return "resource_threshold";
    case TriggerKind::NONE: return "none";
  }
  return "unknown";
}

namespace {
// Map a fault category to its default trigger kind. Deterministic.
TriggerKind default_trigger(FaultCategory c) noexcept {
  switch (c) {
    case FaultCategory::PROCESS_TERMINATION:
    case FaultCategory::PROCESS_CRASH:
    case FaultCategory::PROCESS_STALL:
    case FaultCategory::WORKER_DEATH:
    case FaultCategory::COORDINATOR_DEATH:
      return TriggerKind::COMPLETION_BOUNDARY;
    case FaultCategory::SOCKET_CLOSE:
    case FaultCategory::SOCKET_RESET:
    case FaultCategory::SOCKET_HALF_CLOSE:
    case FaultCategory::CONNECTION_DROP:
      return TriggerKind::PROTOCOL_MESSAGE;
    case FaultCategory::FRAME_TRUNCATION:
    case FaultCategory::FRAME_CORRUPTION:
    case FaultCategory::FRAME_DUPLICATION:
    case FaultCategory::FRAME_REORDER:
    case FaultCategory::FRAME_DELAY:
      return TriggerKind::PROTOCOL_MESSAGE;
    case FaultCategory::STALE_EPOCH:
    case FaultCategory::STALE_BOOT:
    case FaultCategory::STALE_ATTEMPT:
    case FaultCategory::STALE_GENERATION:
    case FaultCategory::STALE_DISPATCH:
      return TriggerKind::EXPLICIT_BARRIER;
    case FaultCategory::DUPLICATE_MESSAGE:
    case FaultCategory::CONFLICTING_DUPLICATE:
      return TriggerKind::PROTOCOL_MESSAGE;
    case FaultCategory::PARTIAL_FILE_WRITE:
    case FaultCategory::PERSISTENCE_TRUNCATION:
    case FaultCategory::PERSISTENCE_CORRUPTION:
    case FaultCategory::BAD_CHECKSUM:
    case FaultCategory::BAD_VERSION:
    case FaultCategory::DISK_WRITE_FAILURE:
      return TriggerKind::PERSISTENCE_PHASE;
    case FaultCategory::RESOURCE_EXHAUSTION:
    case FaultCategory::HOST_MEMORY_PRESSURE:
    case FaultCategory::DEVICE_MEMORY_PRESSURE:
    case FaultCategory::RESERVATION_EXHAUSTION:
    case FaultCategory::CAPACITY_REDUCTION:
    case FaultCategory::CUDA_ALLOCATION_FAILURE:
      return TriggerKind::RESOURCE_THRESHOLD;
    case FaultCategory::RECOVERY_FAILURE:
    case FaultCategory::ROLLBACK_FAILURE:
      return TriggerKind::COMPLETION_BOUNDARY;
    case FaultCategory::SHUTDOWN_RACE:
    case FaultCategory::RESTART_RACE:
    case FaultCategory::CLOCK_SKEW_SIMULATION:
    case FaultCategory::INVALID_STATE_INJECTION:
      return TriggerKind::STATE_TRANSITION;
    default:
      return TriggerKind::EVENT_COUNT;
  }
}
} // namespace

void FaultScheduler::plan(const ChaosCampaign& campaign) {
  schedule_.clear();
  counts_.clear();
  SplitMix64 rng(seed_);
  std::uint64_t order = 0;
  for (auto& f : campaign.fault_schedule) {
    ScheduledInjection si;
    si.id = InjectionId(rng.next());
    si.fault = f.id;
    si.category = f.category;
    si.target = f.target;
    si.timing = f.timing;
    si.trigger = default_trigger(f.category);
    // Parameter overrides.
    if (const std::string* tr = f.param("trigger")) {
      std::string lower = to_lower(*tr);
      if (lower == "event_count") si.trigger = TriggerKind::EVENT_COUNT;
      else if (lower == "state_transition") si.trigger = TriggerKind::STATE_TRANSITION;
      else if (lower == "protocol_message") si.trigger = TriggerKind::PROTOCOL_MESSAGE;
      else if (lower == "dispatch") si.trigger = TriggerKind::DISPATCH;
      else if (lower == "completion_boundary") si.trigger = TriggerKind::COMPLETION_BOUNDARY;
      else if (lower == "persistence_phase") si.trigger = TriggerKind::PERSISTENCE_PHASE;
      else if (lower == "explicit_barrier") si.trigger = TriggerKind::EXPLICIT_BARRIER;
      else if (lower == "resource_threshold") si.trigger = TriggerKind::RESOURCE_THRESHOLD;
      else if (lower == "process_registration") si.trigger = TriggerKind::PROCESS_REGISTRATION;
    }
    si.trigger_name = f.param_or("trigger_name", std::string(trigger_kind_name(si.trigger)));
    if (const std::string* at = f.param("at_count")) {
      std::uint64_t v = 0;
      if (from_hex(*at, v)) si.at_count = v; else si.at_count = 1;
    }
    si.order = order++;
    schedule_.push_back(std::move(si));
  }
}

std::uint64_t FaultScheduler::event_count(std::string_view event) const noexcept {
  for (auto& [name, count] : counts_) {
    if (name == event) return count;
  }
  return 0;
}

void FaultScheduler::register_events() {
  for (auto& si : schedule_) {
    if (si.trigger == TriggerKind::EVENT_COUNT || si.trigger == TriggerKind::PROTOCOL_MESSAGE) {
      auto it = std::find_if(counts_.begin(), counts_.end(), [&](const std::pair<std::string, std::uint64_t>& p) {
        return p.first == si.trigger_name;
      });
      if (it == counts_.end()) counts_.emplace_back(si.trigger_name, 0);
    }
  }
}

std::vector<InjectionId> FaultScheduler::try_fire(TriggerKind trigger, std::string_view name, std::uint64_t count) {
  std::vector<InjectionId> fired;
  for (auto& si : schedule_) {
    if (si.fired) continue;
    if (si.trigger != trigger) continue;
    if (!si.trigger_name.empty() && si.trigger_name != name) continue;
    if (trigger == TriggerKind::EVENT_COUNT && count < si.at_count) continue;
    si.fired = true;
    fired.push_back(si.id);
  }
  return fired;
}

std::vector<InjectionId> FaultScheduler::observe_event(std::string_view event) {
  // bump the count for this event.
  auto it = std::find_if(counts_.begin(), counts_.end(), [&](const std::pair<std::string, std::uint64_t>& p) {
    return p.first == event;
  });
  std::uint64_t count = 1;
  if (it != counts_.end()) { it->second += 1; count = it->second; }
  else { counts_.emplace_back(std::string(event), 1); }
  // Fire any trigger whose name matches the observed event. This lets a single
  // observe_event call drive event-count, state, message, phase, threshold,
  // dispatch and completion-boundary triggers within a declared campaign scope.
  std::vector<InjectionId> fired;
  for (int t = 0; t <= static_cast<int>(TriggerKind::NONE); ++t) {
    auto f = try_fire(static_cast<TriggerKind>(t), event, count);
    fired.insert(fired.end(), f.begin(), f.end());
  }
  return fired;
}

std::vector<InjectionId> FaultScheduler::observe_state(TargetId target, std::string_view state) {
  (void)target;
  return try_fire(TriggerKind::STATE_TRANSITION, state, 1);
}

std::vector<InjectionId> FaultScheduler::observe_message(std::string_view message) {
  return try_fire(TriggerKind::PROTOCOL_MESSAGE, message, 1);
}

bool FaultScheduler::all_fired() const noexcept {
  if (schedule_.empty()) return true;
  for (auto& si : schedule_) if (!si.fired) return false;
  return true;
}

std::size_t FaultScheduler::fired_count() const noexcept {
  std::size_t n = 0;
  for (auto& si : schedule_) if (si.fired) ++n;
  return n;
}

} // namespace chaoslab
