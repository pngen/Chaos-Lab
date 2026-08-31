#include "chaoslab/assertions.h"

#include "chaoslab/text.h"

#include <array>

namespace chaoslab {

namespace {
constexpr std::array<const char*, static_cast<std::size_t>(AssertionKind::Last)> kKindNames = {
  "accepted","rejected","state","terminal","not_terminal","exactly_one_authority",
  "no_stale_mutation","no_double_commit","no_leak","accounting_zero",
  "digest_equal","digest_different","recovery_complete","process_exit",
  "process_alive","resource_baseline"
};
constexpr std::array<const char*, 4> kStatusNames = { "unevaluated","passed","failed","skipped" };
} // namespace

const char* assertion_kind_name(AssertionKind k) noexcept {
  auto i = static_cast<std::size_t>(k);
  return i < kKindNames.size() ? kKindNames[i] : "unknown";
}

const char* assertion_status_name(AssertionStatus s) noexcept {
  auto i = static_cast<std::size_t>(s);
  return i < kStatusNames.size() ? kStatusNames[i] : "unknown";
}

bool parse_assertion_kind(std::string_view s, AssertionKind& out) noexcept {
  std::string lower = to_lower(s);
  for (std::size_t i = 0; i < kKindNames.size(); ++i) {
    if (lower == kKindNames[i]) { out = static_cast<AssertionKind>(i); return true; }
  }
  return false;
}

void AssertionSpec::set_param(std::string key, std::string value) {
  params[std::move(key)] = std::move(value);
}

const std::string* AssertionSpec::param(std::string_view key) const {
  auto it = params.find(std::string(key));
  return it == params.end() ? nullptr : &it->second;
}

namespace {
TargetId spec_target(const AssertionSpec& spec) {
  const std::string* t = spec.param("target");
  if (!t) return TargetId{};
  TargetId id;
  if (TargetId::parse(*t, id)) return id;
  return TargetId{};
}

std::string bool_str(bool b) { return b ? "true" : "false"; }
} // namespace

AssertionResult evaluate_assertion(const AssertionSpec& spec, const RunFacts& facts,
                                   std::string phase) {
  AssertionResult r;
  r.id = spec.id;
  r.kind = spec.kind;
  r.target = spec_target(spec);
  r.phase = std::move(phase);
  bool pass = true;
  std::string obs;

  switch (spec.kind) {
    case AssertionKind::ASSERT_ACCEPTED:
      r.expected = "accepted";
      obs = facts.accepted ? "accepted" : "rejected";
      pass = facts.accepted;
      break;
    case AssertionKind::ASSERT_REJECTED:
      r.expected = "rejected";
      obs = facts.rejected ? "rejected" : "accepted";
      pass = facts.rejected;
      break;
    case AssertionKind::ASSERT_STATE:
      r.expected = facts.expected_state;
      obs = facts.state;
      pass = (facts.state == facts.expected_state);
      break;
    case AssertionKind::ASSERT_TERMINAL:
      r.expected = "terminal";
      obs = facts.terminal ? "terminal" : "active";
      pass = facts.terminal;
      break;
    case AssertionKind::ASSERT_NOT_TERMINAL:
      r.expected = "not_terminal";
      obs = facts.terminal ? "terminal" : "active";
      pass = !facts.terminal;
      break;
    case AssertionKind::ASSERT_EXACTLY_ONE_AUTHORITY:
      r.expected = "authority_count==1";
      obs = "authority_count=" + std::to_string(facts.authority_count);
      pass = facts.authority_count == 1;
      break;
    case AssertionKind::ASSERT_NO_STALE_MUTATION:
      r.expected = "no_stale_mutation";
      obs = facts.stale_mutation ? "stale_mutation_observed" : "no_stale_mutation";
      pass = !facts.stale_mutation;
      break;
    case AssertionKind::ASSERT_NO_DOUBLE_COMMIT:
      r.expected = "no_double_commit";
      obs = facts.double_commit ? "double_commit" : "no_double_commit";
      pass = !facts.double_commit;
      break;
    case AssertionKind::ASSERT_NO_LEAK:
      r.expected = "no_leak";
      obs = facts.leak ? "leak_detected" : "no_leak";
      pass = !facts.leak;
      break;
    case AssertionKind::ASSERT_ACCOUNTING_ZERO:
      r.expected = "accounting==0";
      obs = "accounting=" + std::to_string(facts.accounting);
      pass = facts.accounting_zero;
      break;
    case AssertionKind::ASSERT_DIGEST_EQUAL: {
      r.expected = facts.digest_expected;
      obs = facts.digest_actual;
      pass = (facts.digest_actual == facts.digest_expected);
      break;
    }
    case AssertionKind::ASSERT_DIGEST_DIFFERENT: {
      r.expected = "different_from=" + facts.digest_expected;
      obs = facts.digest_actual;
      pass = (facts.digest_actual != facts.digest_expected);
      break;
    }
    case AssertionKind::ASSERT_RECOVERY_COMPLETE:
      r.expected = "recovery_complete";
      obs = facts.recovery_complete ? "complete" : "incomplete";
      pass = facts.recovery_complete;
      break;
    case AssertionKind::ASSERT_PROCESS_EXIT:
      r.expected = "process_exited";
      obs = facts.process_exited ? ("exited(" + std::to_string(facts.process_exit_code) + ")") : "running";
      pass = facts.process_exited;
      break;
    case AssertionKind::ASSERT_PROCESS_ALIVE:
      r.expected = "process_alive";
      obs = facts.process_alive ? "alive" : "exited";
      pass = facts.process_alive;
      break;
    case AssertionKind::ASSERT_RESOURCE_BASELINE:
      r.expected = "resource_after==baseline";
      obs = "after=" + std::to_string(facts.resource_after) + " baseline=" + std::to_string(facts.resource_baseline);
      pass = facts.resource_baseline_ok;
      break;
    case AssertionKind::Last:
      pass = false;
      break;
  }
  r.observed = std::move(obs);
  r.status = pass ? AssertionStatus::PASSED : AssertionStatus::FAILED;
  r.explanation = std::string(assertion_kind_name(spec.kind)) + " expected=\"" + r.expected +
                  "\" observed=\"" + r.observed + "\"";
  return r;
}

} // namespace chaoslab
