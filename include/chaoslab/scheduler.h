#pragma once
// Deterministic fault scheduling for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/campaign.h"
#include "chaoslab/random.h"

#include <cstdint>
#include <string>
#include <vector>

namespace chaoslab {

/// The condition that triggers a scheduled injection. Anchored to observable
/// events rather than arbitrary sleeps wherever possible.
enum class TriggerKind {
  EVENT_COUNT,          // fires when a named event has occurred N times
  STATE_TRANSITION,     // fires when a named state is entered
  PROTOCOL_MESSAGE,     // fires on a protocol message type
  OPERATION_PHASE,      // fires when an operation enters a phase
  PROCESS_REGISTRATION, // fires on registration of a target
  DISPATCH,             // fires on a dispatch
  COMPLETION_BOUNDARY,  // fires at a completion boundary
  PERSISTENCE_PHASE,    // fires at a persistence phase
  EXPLICIT_BARRIER,     // fires at an explicit barrier
  RESOURCE_THRESHOLD,   // fires above a resource threshold
  NONE
};

const char* trigger_kind_name(TriggerKind k) noexcept;

struct ScheduledInjection {
  InjectionId id;
  FaultId fault;              ///< the FaultSpec this schedules
  FaultCategory category;
  TargetId target;
  Timing timing{Timing::AT_EVENT};
  TriggerKind trigger{TriggerKind::NONE};
  std::string trigger_name;   ///< event/state/message name
  std::uint64_t at_count{1};  ///< for EVENT_COUNT
  std::uint64_t order{0};     ///< deterministic ordering
  bool fired{false};
};

/// Given a campaign's fault schedule, plans deterministic injections and steps
/// on observed events. The same seed + event sequence always yields the same
/// firing decisions.
class FaultScheduler {
public:
  explicit FaultScheduler(std::uint64_t seed = default_seed()) : seed_(seed) {}

  void plan(const ChaosCampaign& campaign);

  /// Observe an event and count it; armed injections that match fire.
  /// Returns the list of injection ids that fired as a result of this event.
  std::vector<InjectionId> observe_event(std::string_view event);

  /// Observe a state transition of a target.
  std::vector<InjectionId> observe_state(TargetId target, std::string_view state);

  /// Observe a protocol message.
  std::vector<InjectionId> observe_message(std::string_view message);

  const std::vector<ScheduledInjection>& schedule() const noexcept { return schedule_; }
  bool all_fired() const noexcept;
  std::size_t fired_count() const noexcept;

  std::uint64_t seed() const noexcept { return seed_; }

  /// The number of times a given event name has been observed.
  std::uint64_t event_count(std::string_view event) const noexcept;

private:
  void register_events();
  std::vector<InjectionId> try_fire(TriggerKind trigger, std::string_view name,
                                    std::uint64_t count);

  std::uint64_t seed_;
  std::vector<ScheduledInjection> schedule_;
  std::vector<std::pair<std::string, std::uint64_t>> counts_;
};

} // namespace chaoslab
