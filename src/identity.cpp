#include "chaoslab/identity.h"

#include "chaoslab/text.h"

namespace chaoslab {

const char* id_kind_name(IdKind kind) noexcept {
  switch (kind) {
    case IdKind::Campaign: return "campaign";
    case IdKind::Scenario: return "scenario";
    case IdKind::Fault: return "fault";
    case IdKind::Injection: return "injection";
    case IdKind::Assertion: return "assertion";
    case IdKind::Evidence: return "evidence";
    case IdKind::Target: return "target";
    case IdKind::Process: return "process";
    case IdKind::Worker: return "worker";
    case IdKind::WorkerBoot: return "workerboot";
    case IdKind::Node: return "node";
    case IdKind::Device: return "device";
    case IdKind::Request: return "request";
    case IdKind::Operation: return "operation";
    case IdKind::Attempt: return "attempt";
    case IdKind::Dispatch: return "dispatch";
    case IdKind::Recovery: return "recovery";
    case IdKind::CoordinatorEpoch: return "coordinatorepoch";
    case IdKind::AttemptGeneration: return "attemptgeneration";
    case IdKind::TargetGeneration: return "targetgeneration";
    case IdKind::CampaignGeneration: return "campaigngeneration";
    case IdKind::Last: return "last";
  }
  return "unknown";
}

template <IdKind Kind>
std::string Id<Kind>::to_string() const noexcept {
  return to_hex(value_);
}

template <IdKind Kind>
std::string Id<Kind>::str() const noexcept {
  return std::string(id_kind_name(Kind)) + ":" + to_string();
}

template <IdKind Kind>
bool Id<Kind>::parse(std::string_view s, Id& out) noexcept {
  std::string_view v = s;
  // Accept optional "kind:" prefix; if present it must match this kind.
  auto colon = s.find(':');
  if (colon != std::string_view::npos) {
    std::string_view k = s.substr(0, colon);
    if (k != id_kind_name(Kind)) return false;
    v = s.substr(colon + 1);
  }
  std::uint64_t value = 0;
  if (!from_hex(v, value)) return false;
  out = Id(value);
  return true;
}

// Force instantiation for all identity types.
template class Id<IdKind::Campaign>;
template class Id<IdKind::Scenario>;
template class Id<IdKind::Fault>;
template class Id<IdKind::Injection>;
template class Id<IdKind::Assertion>;
template class Id<IdKind::Evidence>;
template class Id<IdKind::Target>;
template class Id<IdKind::Process>;
template class Id<IdKind::Worker>;
template class Id<IdKind::WorkerBoot>;
template class Id<IdKind::Node>;
template class Id<IdKind::Device>;
template class Id<IdKind::Request>;
template class Id<IdKind::Operation>;
template class Id<IdKind::Attempt>;
template class Id<IdKind::Dispatch>;
template class Id<IdKind::Recovery>;
template class Id<IdKind::CoordinatorEpoch>;
template class Id<IdKind::AttemptGeneration>;
template class Id<IdKind::TargetGeneration>;
template class Id<IdKind::CampaignGeneration>;

} // namespace chaoslab
