#include "chaoslab/fault.h"

#include "chaoslab/text.h"

#include <array>

namespace chaoslab {

namespace {
constexpr std::array<const char*, static_cast<std::size_t>(FaultCategory::Last)> kFaultNames = {
  "process_termination","process_crash","process_stall","coordinator_death","worker_death",
  "socket_close","socket_reset","socket_half_close","connection_drop",
  "frame_truncation","frame_corruption","frame_duplication","frame_reorder","frame_delay",
  "stale_epoch","stale_boot","stale_attempt","stale_generation","stale_dispatch",
  "duplicate_message","conflicting_duplicate",
  "partial_file_write","persistence_truncation","persistence_corruption","bad_checksum","bad_version","disk_write_failure",
  "resource_exhaustion","host_memory_pressure","device_memory_pressure","reservation_exhaustion","capacity_reduction",
  "cuda_application_failure","cuda_verification_failure","cuda_allocation_failure",
  "dependency_failure","recovery_failure","rollback_failure",
  "shutdown_race","restart_race","clock_skew_simulation","invalid_state_injection"
};

constexpr std::array<const char*, 6> kSourceNames = {
  "injected","secondary","target_response","recovery","environment","unspecified"
};

constexpr std::array<const char*, 18> kScopeNames = {
  "process","worker","coordinator","connection","persistence","file",
  "allocation","reservation","accelerator","host_memory","device_memory",
  "host","device","generation","epoch","protocol","authority","unspecified"
};

constexpr std::array<const char*, 4> kSeverityNames = { "low","medium","high","critical" };

constexpr std::array<const char*, 8> kTimingNames = {
  "immediate","delayed","at_event","at_barrier","after_side_effect","before_ack","at_phase","unspecified"
};
} // namespace

const char* fault_category_name(FaultCategory c) noexcept {
  auto i = static_cast<std::size_t>(c);
  return i < kFaultNames.size() ? kFaultNames[i] : "unknown";
}

const char* fault_source_name(FaultSource s) noexcept {
  auto i = static_cast<std::size_t>(s);
  return i < kSourceNames.size() ? kSourceNames[i] : "unknown";
}

const char* fault_scope_name(FaultScope s) noexcept {
  auto i = static_cast<std::size_t>(s);
  return i < kScopeNames.size() ? kScopeNames[i] : "unknown";
}

const char* severity_name(Severity s) noexcept {
  auto i = static_cast<std::size_t>(s);
  return i < kSeverityNames.size() ? kSeverityNames[i] : "unknown";
}

const char* timing_name(Timing t) noexcept {
  auto i = static_cast<std::size_t>(t);
  return i < kTimingNames.size() ? kTimingNames[i] : "unknown";
}

bool parse_fault_category(std::string_view s, FaultCategory& out) noexcept {
  std::string lower = to_lower(s);
  for (std::size_t i = 0; i < kFaultNames.size(); ++i) {
    if (lower == kFaultNames[i]) { out = static_cast<FaultCategory>(i); return true; }
  }
  return false;
}

bool parse_severity(std::string_view s, Severity& out) noexcept {
  std::string lower = to_lower(s);
  for (std::size_t i = 0; i < kSeverityNames.size(); ++i) {
    if (lower == kSeverityNames[i]) { out = static_cast<Severity>(i); return true; }
  }
  return false;
}

const std::string* FaultSpec::param(std::string_view key) const noexcept {
  auto it = params.find(std::string(key));
  return it == params.end() ? nullptr : &it->second;
}

std::string FaultSpec::param_or(std::string_view key, std::string fallback) const {
  const std::string* p = param(key);
  return p ? *p : std::move(fallback);
}

void FaultSpec::set_param(std::string key, std::string value) {
  params[std::move(key)] = std::move(value);
}

} // namespace chaoslab
