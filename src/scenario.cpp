#include "chaoslab/scenario.h"

#include "chaoslab/text.h"

namespace chaoslab {

const char* scenario_op_name(ScenarioOp op) noexcept {
  switch (op) {
    case ScenarioOp::START_PROCESS: return "start_process";
    case ScenarioOp::WAIT_FOR_REGISTRATION: return "wait_for_registration";
    case ScenarioOp::DISPATCH_WORK: return "dispatch_work";
    case ScenarioOp::CAPTURE_AUTHORITY: return "capture_authority";
    case ScenarioOp::INJECT_FAULT: return "inject_fault";
    case ScenarioOp::KILL_PROCESS: return "kill_process";
    case ScenarioOp::CLOSE_SOCKET: return "close_socket";
    case ScenarioOp::CORRUPT_FRAME: return "corrupt_frame";
    case ScenarioOp::TRUNCATE_FILE: return "truncate_file";
    case ScenarioOp::REDUCE_CAPACITY: return "reduce_capacity";
    case ScenarioOp::ALLOCATE_MEMORY: return "allocate_memory";
    case ScenarioOp::RELEASE_MEMORY: return "release_memory";
    case ScenarioOp::RESTART_PROCESS: return "restart_process";
    case ScenarioOp::ROLL_EPOCH: return "roll_epoch";
    case ScenarioOp::REPLAY_STALE_MESSAGE: return "replay_stale_message";
    case ScenarioOp::WAIT_FOR_STATE: return "wait_for_state";
    case ScenarioOp::ASSERT_STATE: return "assert_state";
    case ScenarioOp::ASSERT_REJECTED: return "assert_rejected";
    case ScenarioOp::ASSERT_NO_LEAK: return "assert_no_leak";
    case ScenarioOp::ASSERT_EXACTLY_ONE_AUTHORITY: return "assert_exactly_one_authority";
    case ScenarioOp::SAVE: return "save";
    case ScenarioOp::RELOAD: return "reload";
    case ScenarioOp::REPLAY: return "replay";
    case ScenarioOp::CLEANUP: return "cleanup";
    case ScenarioOp::NOP: return "nop";
  }
  return "unknown";
}

std::string ScenarioStep::param_or(std::string_view key, std::string fallback) const {
  auto it = params.find(std::string(key));
  return it == params.end() ? std::move(fallback) : it->second;
}

namespace {
constexpr TargetId kGlobal{0};
}

ScenarioBuilder::ScenarioBuilder(std::uint64_t seed) : seed_(seed) {
  s_.campaign.seed = seed;
}

ScenarioBuilder& ScenarioBuilder::seed(std::uint64_t s) {
  // seed_ is captured const at construction for deterministic campaigns; but a
  // to-be-built campaign can be re-seeded before build. Keep the campaign seed
  // authoritative.
  const_cast<std::uint64_t&>(seed_) = s;
  s_.campaign.seed = s;
  return *this;
}

ScenarioBuilder& ScenarioBuilder::purpose(std::string p) {
  s_.campaign.purpose = std::move(p);
  return *this;
}

TargetId ScenarioBuilder::new_target_id(bool owned) {
  std::uint64_t v = (static_cast<std::uint64_t>(owned ? 1 : 0) << 63) | target_counter_;
  ++target_counter_;
  return TargetId(v);
}

TargetSpec& ScenarioBuilder::target_ref(TargetId id) {
  for (auto& t : s_.campaign.targets) {
    if (t.id == id) return t;
  }
  return s_.campaign.targets.front();
}

const TargetSpec& ScenarioBuilder::target_ref(TargetId id) const {
  for (auto& t : s_.campaign.targets) {
    if (t.id == id) return t;
  }
  return s_.campaign.targets.front();
}

void ScenarioBuilder::add_step(ScenarioOp op, TargetId t, std::map<std::string, std::string> params) {
  ScenarioStep step;
  step.op = op;
  step.target = t;
  step.params = std::move(params);
  s_.steps.push_back(std::move(step));
}

void ScenarioBuilder::add_fault(FaultCategory c, FaultScope scope, TargetId t,
                                std::map<std::string, std::string> params) {
  FaultSpec f;
  f.id = FaultId(fault_counter_);
  ++fault_counter_;
  f.category = c;
  f.source = FaultSource::INJECTED;
  f.scope = scope;
  f.severity = Severity::HIGH;
  f.timing = Timing::AT_EVENT;
  f.target = t;
  f.targets = {t};
  f.params = std::move(params);
  s_.campaign.fault_schedule.push_back(std::move(f));
}

void ScenarioBuilder::add_assertion(AssertionKind k, TargetId t, std::map<std::string, std::string> params) {
  AssertionSpec a;
  a.id = AssertionId(assertion_counter_);
  ++assertion_counter_;
  a.kind = k;
  a.params = std::move(params);
  a.description = std::string(assertion_kind_name(k));
  if (t != kGlobal) a.set_param("target", t.str());
  s_.campaign.assertions.push_back(std::move(a));
}

ScenarioBuilder& ScenarioBuilder::envelope(ResourceEnvelope env) {
  s_.campaign.envelope = std::move(env);
  return *this;
}

// --- target declaration ----------------------------------------------------
ScenarioBuilder& ScenarioBuilder::target_process(std::string name, std::string executable,
                                                 std::vector<std::string> args) {
  TargetId id = new_target_id(true);
  TargetSpec t;
  t.id = id; t.kind = TargetKind::PROCESS; t.name = name; t.executable = executable;
  t.arguments = std::move(args); t.owns_process = true;
  s_.campaign.targets.push_back(std::move(t));
  names_[name] = id;
  return *this;
}

ScenarioBuilder& ScenarioBuilder::target_worker(std::string name, std::string executable,
                                                std::vector<std::string> args) {
  TargetId id = new_target_id(true);
  TargetSpec t;
  t.id = id; t.kind = TargetKind::WORKER; t.name = name; t.executable = executable;
  t.arguments = std::move(args); t.owns_process = true;
  s_.campaign.targets.push_back(std::move(t));
  names_[name] = id;
  return *this;
}

ScenarioBuilder& ScenarioBuilder::target_coordinator(std::string name, std::string executable,
                                                     std::vector<std::string> args) {
  TargetId id = new_target_id(true);
  TargetSpec t;
  t.id = id; t.kind = TargetKind::COORDINATOR; t.name = name; t.executable = executable;
  t.arguments = std::move(args); t.owns_process = true;
  s_.campaign.targets.push_back(std::move(t));
  names_[name] = id;
  return *this;
}

ScenarioBuilder& ScenarioBuilder::target_proxy(std::string name, std::string executable) {
  TargetId id = new_target_id(true);
  TargetSpec t;
  t.id = id; t.kind = TargetKind::TRANSPORT_PROXY; t.name = name; t.executable = std::move(executable);
  s_.campaign.targets.push_back(std::move(t));
  names_[name] = id;
  return *this;
}

ScenarioBuilder& ScenarioBuilder::target_persistence(std::string name, std::string path) {
  TargetId id = new_target_id(true);
  TargetSpec t;
  t.id = id; t.kind = TargetKind::PERSISTENCE_FILE; t.name = name; t.temp_dir = std::move(path);
  s_.campaign.targets.push_back(std::move(t));
  names_[name] = id;
  return *this;
}

ScenarioBuilder& ScenarioBuilder::target_device(std::string name, std::string kind) {
  TargetId id = new_target_id(true);
  TargetSpec t;
  t.id = id; t.kind = TargetKind::DEVICE; t.name = name; t.executable = std::move(kind);
  s_.campaign.targets.push_back(std::move(t));
  names_[name] = id;
  return *this;
}

// --- schedule ------------------------------------------------------------
ScenarioBuilder& ScenarioBuilder::start_process(TargetId t) { add_step(ScenarioOp::START_PROCESS, t); return *this; }
ScenarioBuilder& ScenarioBuilder::wait_for_registration(TargetId t) { add_step(ScenarioOp::WAIT_FOR_REGISTRATION, t); return *this; }
ScenarioBuilder& ScenarioBuilder::dispatch_work(TargetId t) { add_step(ScenarioOp::DISPATCH_WORK, t); return *this; }
ScenarioBuilder& ScenarioBuilder::capture_authority(TargetId t) { add_step(ScenarioOp::CAPTURE_AUTHORITY, t); return *this; }
ScenarioBuilder& ScenarioBuilder::kill_process(TargetId t) { add_step(ScenarioOp::KILL_PROCESS, t); add_fault(FaultCategory::PROCESS_TERMINATION, FaultScope::PROCESS, t, {}); return *this; }
ScenarioBuilder& ScenarioBuilder::close_socket(TargetId t) { add_step(ScenarioOp::CLOSE_SOCKET, t); add_fault(FaultCategory::SOCKET_CLOSE, FaultScope::CONNECTION, t, {}); return *this; }
ScenarioBuilder& ScenarioBuilder::corrupt_frame(TargetId t) { add_step(ScenarioOp::CORRUPT_FRAME, t); add_fault(FaultCategory::FRAME_CORRUPTION, FaultScope::CONNECTION, t, {}); return *this; }
ScenarioBuilder& ScenarioBuilder::truncate_file(TargetId t, int offset) { add_step(ScenarioOp::TRUNCATE_FILE, t, {{"offset", std::to_string(offset)}}); add_fault(FaultCategory::PERSISTENCE_TRUNCATION, FaultScope::FILE, t, {{"offset", std::to_string(offset)}}); return *this; }
ScenarioBuilder& ScenarioBuilder::reduce_capacity(TargetId t, int percent) { add_step(ScenarioOp::REDUCE_CAPACITY, t, {{"percent", std::to_string(percent)}}); add_fault(FaultCategory::CAPACITY_REDUCTION, FaultScope::DEVICE, t, {{"percent", std::to_string(percent)}}); return *this; }
ScenarioBuilder& ScenarioBuilder::allocate_memory(TargetId t, std::uint64_t bytes) { add_step(ScenarioOp::ALLOCATE_MEMORY, t, {{"bytes", std::to_string(bytes)}}); return *this; }
ScenarioBuilder& ScenarioBuilder::release_memory(TargetId t) { add_step(ScenarioOp::RELEASE_MEMORY, t); return *this; }
ScenarioBuilder& ScenarioBuilder::restart_process(TargetId t) { add_step(ScenarioOp::RESTART_PROCESS, t); return *this; }
ScenarioBuilder& ScenarioBuilder::roll_epoch(TargetId t) { add_step(ScenarioOp::ROLL_EPOCH, t); return *this; }
ScenarioBuilder& ScenarioBuilder::replay_stale_message(TargetId t) { add_step(ScenarioOp::REPLAY_STALE_MESSAGE, t); return *this; }
ScenarioBuilder& ScenarioBuilder::wait_for_state(TargetId t, std::string state) { add_step(ScenarioOp::WAIT_FOR_STATE, t, {{"state", std::move(state)}}); return *this; }
ScenarioBuilder& ScenarioBuilder::save(TargetId t) { add_step(ScenarioOp::SAVE, t); return *this; }
ScenarioBuilder& ScenarioBuilder::reload(TargetId t) { add_step(ScenarioOp::RELOAD, t); return *this; }
ScenarioBuilder& ScenarioBuilder::replay(TargetId t) { add_step(ScenarioOp::REPLAY, t); return *this; }
ScenarioBuilder& ScenarioBuilder::cleanup(TargetId t) { add_step(ScenarioOp::CLEANUP, t); return *this; }

ScenarioBuilder& ScenarioBuilder::inject_fault(FaultCategory c, TargetId t) {
  FaultScope scope = FaultScope::PROCESS;
  switch (c) {
    case FaultCategory::SOCKET_CLOSE: case FaultCategory::SOCKET_RESET:
    case FaultCategory::SOCKET_HALF_CLOSE: case FaultCategory::CONNECTION_DROP:
    case FaultCategory::FRAME_TRUNCATION: case FaultCategory::FRAME_CORRUPTION:
    case FaultCategory::FRAME_DUPLICATION: case FaultCategory::FRAME_REORDER:
    case FaultCategory::FRAME_DELAY: scope = FaultScope::CONNECTION; break;
    case FaultCategory::STALE_EPOCH: case FaultCategory::STALE_BOOT:
    case FaultCategory::STALE_ATTEMPT: case FaultCategory::STALE_GENERATION:
    case FaultCategory::STALE_DISPATCH: scope = FaultScope::AUTHORITY; break;
    case FaultCategory::DUPLICATE_MESSAGE: case FaultCategory::CONFLICTING_DUPLICATE:
      scope = FaultScope::PROTOCOL; break;
    case FaultCategory::PARTIAL_FILE_WRITE: case FaultCategory::PERSISTENCE_TRUNCATION:
    case FaultCategory::PERSISTENCE_CORRUPTION: case FaultCategory::BAD_CHECKSUM:
    case FaultCategory::BAD_VERSION: case FaultCategory::DISK_WRITE_FAILURE:
      scope = FaultScope::PERSISTENCE; break;
    case FaultCategory::HOST_MEMORY_PRESSURE: scope = FaultScope::HOST_MEMORY; break;
    case FaultCategory::DEVICE_MEMORY_PRESSURE: scope = FaultScope::DEVICE_MEMORY; break;
    case FaultCategory::RESOURCE_EXHAUSTION: scope = FaultScope::RESERVATION; break;
    case FaultCategory::RESERVATION_EXHAUSTION: scope = FaultScope::RESERVATION; break;
    case FaultCategory::CAPACITY_REDUCTION: scope = FaultScope::DEVICE; break;
    case FaultCategory::CUDA_APPLICATION_FAILURE: case FaultCategory::CUDA_VERIFICATION_FAILURE:
    case FaultCategory::CUDA_ALLOCATION_FAILURE: scope = FaultScope::ACCELERATOR; break;
    case FaultCategory::COORDINATOR_DEATH: scope = FaultScope::COORDINATOR; break;
    case FaultCategory::WORKER_DEATH: scope = FaultScope::WORKER; break;
    default: scope = FaultScope::PROCESS; break;
  }
  add_step(ScenarioOp::INJECT_FAULT, t, {{"category", fault_category_name(c)}});
  add_fault(c, scope, t, {});
  return *this;
}

// --- assertions ----------------------------------------------------------
ScenarioBuilder& ScenarioBuilder::assert_state(TargetId t, std::string expect) {
  add_step(ScenarioOp::ASSERT_STATE, t, {{"state", expect}});
  add_assertion(AssertionKind::ASSERT_STATE, t, {{"state", std::move(expect)}});
  return *this;
}
ScenarioBuilder& ScenarioBuilder::assert_rejected(TargetId t, std::string what) {
  add_step(ScenarioOp::ASSERT_REJECTED, t, {{"what", what}});
  add_assertion(AssertionKind::ASSERT_REJECTED, t, {{"what", std::move(what)}});
  return *this;
}
ScenarioBuilder& ScenarioBuilder::assert_no_leak(TargetId t) {
  add_assertion(AssertionKind::ASSERT_NO_LEAK, t, {});
  return *this;
}
ScenarioBuilder& ScenarioBuilder::assert_exactly_one_authority() {
  add_assertion(AssertionKind::ASSERT_EXACTLY_ONE_AUTHORITY, kGlobal, {});
  return *this;
}
ScenarioBuilder& ScenarioBuilder::assert_no_double_commit() {
  add_assertion(AssertionKind::ASSERT_NO_DOUBLE_COMMIT, kGlobal, {});
  return *this;
}
ScenarioBuilder& ScenarioBuilder::assert_process_alive(TargetId t) {
  add_assertion(AssertionKind::ASSERT_PROCESS_ALIVE, t, {});
  return *this;
}
ScenarioBuilder& ScenarioBuilder::assert_process_exit(TargetId t) {
  add_assertion(AssertionKind::ASSERT_PROCESS_EXIT, t, {});
  return *this;
}
ScenarioBuilder& ScenarioBuilder::assert_accounting_zero(TargetId t) {
  add_assertion(AssertionKind::ASSERT_ACCOUNTING_ZERO, t, {});
  return *this;
}
ScenarioBuilder& ScenarioBuilder::assert_recovery_complete(TargetId t) {
  add_assertion(AssertionKind::ASSERT_RECOVERY_COMPLETE, t, {});
  return *this;
}

Scenario ScenarioBuilder::build() {
  if (s_.campaign.id.value() == 0) s_.campaign.id = CampaignId(seed_ ^ 0xCA11CA11CA11CA11ull);
  if (s_.campaign.generation.value() == 0) s_.campaign.generation = CampaignGeneration(1);
  return std::move(s_);
}

} // namespace chaoslab
