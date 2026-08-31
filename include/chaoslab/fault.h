#pragma once
// Strongly typed fault model for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/identity.h"

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace chaoslab {

/// Category of fault a campaign may inject. Kept as a stable, closed enum.
enum class FaultCategory {
  PROCESS_TERMINATION,
  PROCESS_CRASH,
  PROCESS_STALL,
  COORDINATOR_DEATH,
  WORKER_DEATH,
  SOCKET_CLOSE,
  SOCKET_RESET,
  SOCKET_HALF_CLOSE,
  CONNECTION_DROP,
  FRAME_TRUNCATION,
  FRAME_CORRUPTION,
  FRAME_DUPLICATION,
  FRAME_REORDER,
  FRAME_DELAY,
  STALE_EPOCH,
  STALE_BOOT,
  STALE_ATTEMPT,
  STALE_GENERATION,
  STALE_DISPATCH,
  DUPLICATE_MESSAGE,
  CONFLICTING_DUPLICATE,
  PARTIAL_FILE_WRITE,
  PERSISTENCE_TRUNCATION,
  PERSISTENCE_CORRUPTION,
  BAD_CHECKSUM,
  BAD_VERSION,
  DISK_WRITE_FAILURE,
  RESOURCE_EXHAUSTION,
  HOST_MEMORY_PRESSURE,
  DEVICE_MEMORY_PRESSURE,
  RESERVATION_EXHAUSTION,
  CAPACITY_REDUCTION,
  CUDA_APPLICATION_FAILURE,
  CUDA_VERIFICATION_FAILURE,
  CUDA_ALLOCATION_FAILURE,
  DEPENDENCY_FAILURE,
  RECOVERY_FAILURE,
  ROLLBACK_FAILURE,
  SHUTDOWN_RACE,
  RESTART_RACE,
  CLOCK_SKEW_SIMULATION,
  INVALID_STATE_INJECTION,
  Last
};

/// Where a fault originates from. Chaos Lab distinguishes injected faults from
/// secondary failures that naturally result from the injected condition.
enum class FaultSource {
  INJECTED,          // deliberately created by Chaos Lab
  SECONDARY,         // naturally resulting secondary failure
  TARGET_RESPONSE,   // the target runtime's own reaction
  RECOVERY,          // from a recovery action
  ENVIRONMENT,       // host/OS/accelerator environment
  UNSPECIFIED
};

/// What subsystem / scope a fault targets. A fault must not escape its scope.
enum class FaultScope {
  PROCESS, WORKER, COORDINATOR, CONNECTION, PERSISTENCE, FILE,
  ALLOCATION, RESERVATION, ACCELERATOR, HOST_MEMORY, DEVICE_MEMORY,
  HOST, DEVICE, GENERATION, EPOCH, PROTOCOL, AUTHORITY, UNSPECIFIED
};

enum class Severity { LOW, MEDIUM, HIGH, CRITICAL };

/// When a fault fires relative to a trigger.
enum class Timing { IMMEDIATE, DELAYED, AT_EVENT, AT_BARRIER, AFTER_SIDE_EFFECT, BEFORE_ACK, AT_PHASE, UNSPECIFIED };

const char* fault_category_name(FaultCategory c) noexcept;
const char* fault_source_name(FaultSource s) noexcept;
const char* fault_scope_name(FaultScope s) noexcept;
const char* severity_name(Severity s) noexcept;
const char* timing_name(Timing t) noexcept;

/// Parse a category from name (case-insensitive). Returns false on unknown.
bool parse_fault_category(std::string_view s, FaultCategory& out) noexcept;
bool parse_severity(std::string_view s, Severity& out) noexcept;

/// A fully described fault. Source/scope/severity/timing/expected-effect are
/// kept separate from the injected category itself.
struct FaultSpec {
  FaultId id;
  FaultCategory category{FaultCategory::PROCESS_TERMINATION};
  FaultSource source{FaultSource::INJECTED};
  FaultScope scope{FaultScope::PROCESS};
  Severity severity{Severity::HIGH};
  Timing timing{Timing::AT_EVENT};
  TargetId target;                    ///< primary owned target
  std::vector<TargetId> targets;      ///< full owned target set (may equal {target})
  std::string detail;                 ///< human readable purpose
  std::map<std::string, std::string> params; ///< key/value fault parameters

  // Convenience accessors.
  const std::string* param(std::string_view key) const noexcept;
  std::string param_or(std::string_view key, std::string fallback) const;
  void set_param(std::string key, std::string value);
};

} // namespace chaoslab
