#pragma once
// Typed programmatic scenario DSL / builder for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/campaign.h"
#include "chaoslab/random.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace chaoslab {

enum class ScenarioOp : std::uint32_t {
  START_PROCESS, WAIT_FOR_REGISTRATION, DISPATCH_WORK, CAPTURE_AUTHORITY,
  INJECT_FAULT, KILL_PROCESS, CLOSE_SOCKET, CORRUPT_FRAME, TRUNCATE_FILE,
  REDUCE_CAPACITY, ALLOCATE_MEMORY, RELEASE_MEMORY, RESTART_PROCESS,
  ROLL_EPOCH, REPLAY_STALE_MESSAGE, WAIT_FOR_STATE, ASSERT_STATE,
  ASSERT_REJECTED, ASSERT_NO_LEAK, ASSERT_EXACTLY_ONE_AUTHORITY,
  SAVE, RELOAD, REPLAY, CLEANUP, NOP
};

const char* scenario_op_name(ScenarioOp op) noexcept;

struct ScenarioStep {
  ScenarioOp op{ScenarioOp::NOP};
  TargetId target;
  FaultCategory fault{FaultCategory::PROCESS_TERMINATION};
  std::string state;
  std::map<std::string, std::string> params;

  std::string param_or(std::string_view key, std::string fallback) const;
};

struct Scenario {
  ChaosCampaign campaign;
  std::vector<ScenarioStep> steps;
};

class ScenarioBuilder {
public:
  explicit ScenarioBuilder(std::uint64_t seed = default_seed());

  ScenarioBuilder& seed(std::uint64_t s);
  ScenarioBuilder& purpose(std::string p);

  ScenarioBuilder& target_process(std::string name, std::string executable,
                                  std::vector<std::string> args = {});
  ScenarioBuilder& target_worker(std::string name, std::string executable,
                                 std::vector<std::string> args = {});
  ScenarioBuilder& target_coordinator(std::string name, std::string executable,
                                      std::vector<std::string> args = {});
  ScenarioBuilder& target_proxy(std::string name, std::string executable = {});
  ScenarioBuilder& target_persistence(std::string name, std::string path);
  ScenarioBuilder& target_device(std::string name, std::string kind);

  ScenarioBuilder& start_process(TargetId t);
  ScenarioBuilder& wait_for_registration(TargetId t);
  ScenarioBuilder& dispatch_work(TargetId t);
  ScenarioBuilder& capture_authority(TargetId t);
  ScenarioBuilder& inject_fault(FaultCategory c, TargetId t);
  ScenarioBuilder& kill_process(TargetId t);
  ScenarioBuilder& close_socket(TargetId t);
  ScenarioBuilder& corrupt_frame(TargetId t);
  ScenarioBuilder& truncate_file(TargetId t, int offset);
  ScenarioBuilder& reduce_capacity(TargetId t, int percent);
  ScenarioBuilder& allocate_memory(TargetId t, std::uint64_t bytes);
  ScenarioBuilder& release_memory(TargetId t);
  ScenarioBuilder& restart_process(TargetId t);
  ScenarioBuilder& roll_epoch(TargetId t);
  ScenarioBuilder& replay_stale_message(TargetId t);
  ScenarioBuilder& wait_for_state(TargetId t, std::string state);
  ScenarioBuilder& save(TargetId t);
  ScenarioBuilder& reload(TargetId t);
  ScenarioBuilder& replay(TargetId t);
  ScenarioBuilder& cleanup(TargetId t);

  ScenarioBuilder& assert_state(TargetId t, std::string expect);
  ScenarioBuilder& assert_rejected(TargetId t, std::string what);
  ScenarioBuilder& assert_no_leak(TargetId t);
  ScenarioBuilder& assert_exactly_one_authority();
  ScenarioBuilder& assert_no_double_commit();
  ScenarioBuilder& assert_process_alive(TargetId t);
  ScenarioBuilder& assert_process_exit(TargetId t);
  ScenarioBuilder& assert_accounting_zero(TargetId t);
  ScenarioBuilder& assert_recovery_complete(TargetId t);

  ScenarioBuilder& envelope(ResourceEnvelope env);

  Scenario build();

private:
  TargetId new_target_id(bool owned);
  TargetSpec& target_ref(TargetId id);
  const TargetSpec& target_ref(TargetId id) const;
  TargetId reserve_global_target(std::string name);
  void add_step(ScenarioOp op, TargetId t, std::map<std::string, std::string> params = {});
  void add_fault(FaultCategory c, FaultScope scope, TargetId t, std::map<std::string, std::string> params);
  void add_assertion(AssertionKind k, TargetId t, std::map<std::string, std::string> params);

  Scenario s_;
  std::map<std::string, TargetId> names_;
  std::uint64_t target_counter_{1};
  std::uint64_t fault_counter_{1};
  std::uint64_t assertion_counter_{1};
  const std::uint64_t seed_;
};

} // namespace chaoslab
