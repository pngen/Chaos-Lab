#include "chaoslab/authority.h"

#include "chaoslab/text.h"

#include <vector>

namespace chaoslab {

namespace {
std::string pair(std::string_view k, std::uint64_t v) { return std::string(k) + "=" + to_hex(v); }
} // namespace

AuthorityEnvelope default_authority() { return AuthorityEnvelope{}; }

std::string AuthorityEnvelope::to_text() const {
  std::string s;
  s += pair("epoch", epoch.value());      s += ';';
  s += pair("worker", worker.value());    s += ';';
  s += pair("boot", boot.value());        s += ';';
  s += pair("attempt", attempt.value());  s += ';';
  s += pair("attempt_gen", attempt_gen.value()); s += ';';
  s += pair("dispatch", dispatch.value()); s += ';';
  s += pair("target_gen", target_gen.value());
  return s;
}

Status AuthorityEnvelope::parse(std::string_view text, AuthorityEnvelope& out) {
  out = AuthorityEnvelope{};
  std::uint64_t values[7] = {0};
  const char* keys[7] = {"epoch","worker","boot","attempt","attempt_gen","dispatch","target_gen"};
  auto parts = split(text, ';');
  if (parts.size() != 7) return Status::error(StatusCode::invalid_argument, "authority envelope must have 7 fields");
  for (std::size_t i = 0; i < parts.size(); ++i) {
    std::size_t eq = parts[i].find('=');
    if (eq == std::string::npos) return Status::error(StatusCode::invalid_argument, "malformed authority field");
    if (parts[i].substr(0, eq) != keys[i]) return Status::error(StatusCode::invalid_argument, "authority key mismatch");
    if (!from_hex(parts[i].substr(eq + 1), values[i])) return Status::error(StatusCode::invalid_argument, "authority value not hex");
  }
  out.epoch = CoordinatorEpoch(values[0]);
  out.worker = WorkerId(values[1]);
  out.boot = WorkerBootId(values[2]);
  out.attempt = AttemptId(values[3]);
  out.attempt_gen = AttemptGeneration(values[4]);
  out.dispatch = DispatchId(values[5]);
  out.target_gen = TargetGeneration(values[6]);
  return Status::ok();
}

bool authority_matches(const AuthorityEnvelope& current, const AuthorityEnvelope& candidate) {
  return current == candidate;
}

namespace {
bool older(std::uint64_t cur, std::uint64_t cand) {
  return cand != 0 && cand < cur; // 0 means "absent" in the candidate; not stale.
}
} // namespace

bool authority_is_stale(const AuthorityEnvelope& current, const AuthorityEnvelope& candidate) {
  if (older(current.epoch.value(), candidate.epoch.value())) return true;
  if (older(current.boot.value(), candidate.boot.value())) return true;
  if (older(current.attempt.value(), candidate.attempt.value())) return true;
  if (older(current.attempt_gen.value(), candidate.attempt_gen.value())) return true;
  if (older(current.dispatch.value(), candidate.dispatch.value())) return true;
  if (older(current.target_gen.value(), candidate.target_gen.value())) return true;
  return false;
}

std::string stale_dimension(const AuthorityEnvelope& current, const AuthorityEnvelope& candidate) {
  if (older(current.epoch.value(), candidate.epoch.value())) return "epoch";
  if (older(current.boot.value(), candidate.boot.value())) return "boot";
  if (older(current.attempt.value(), candidate.attempt.value())) return "attempt";
  if (older(current.attempt_gen.value(), candidate.attempt_gen.value())) return "attempt_gen";
  if (older(current.dispatch.value(), candidate.dispatch.value())) return "dispatch";
  if (older(current.target_gen.value(), candidate.target_gen.value())) return "target_gen";
  return "";
}

AuthorityEnvelope stale_combination(int kind, const AuthorityEnvelope& current) {
  AuthorityEnvelope c = current;
  switch (kind) {
    case 0: c.boot = WorkerBootId(current.boot.value() - 1); break;              // fresh epoch + stale boot
    case 1: c.attempt = AttemptId(current.attempt.value() - 1); break;          // fresh boot + stale attempt
    case 2: c.dispatch = DispatchId(current.dispatch.value() - 1); break;       // current attempt + stale dispatch
    case 3: c.target_gen = TargetGeneration(current.target_gen.value() - 1); break; // stale generation
    case 4: /* duplicate current completion: identical */ break;
    case 5: c.epoch = CoordinatorEpoch(current.epoch.value() + 1); break;       // conflicting duplicate
    default: break;
  }
  return c;
}

} // namespace chaoslab
