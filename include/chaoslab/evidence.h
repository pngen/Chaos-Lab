#pragma once
// Structured evidence collection and deterministic serialization.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/digest.h"
#include "chaoslab/identity.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace chaoslab {

enum class EvidenceKind {
  CAMPAIGN_DEFINITION, PROCESS_LAUNCH, PROCESS_EXIT, STDOUT_STDERR, PROTOCOL_EVENT,
  INJECTION, ASSERTION, STATE_SNAPSHOT, AUTHORITY_ENVELOPE, PERSISTENCE_DIGEST,
  RECOVERY_EVENT, CLEANUP, FINAL_RESULT, MACHINE_INFO, TARGET_VERSION, SEED, MISC,
  Last
};

const char* evidence_kind_name(EvidenceKind k) noexcept;

/// One recorded evidence item. Everything that must be reproducible is captured
/// into a flat, ordered structure with no hidden reference to live state.
struct EvidenceRecord {
  EvidenceId id;
  EvidenceKind kind{EvidenceKind::MISC};
  std::int64_t seq{0};
  std::uint64_t timestamp_ns{0};       ///< observed wall-clock (host) timestamp
  std::string phase;                   ///< campaign phase at capture
  std::map<std::string, std::string> fields; ///< ordered key/value facts
  std::string payload;                 ///< opaque (stdout/stderr, frame bytes)

  void set(std::string key, std::string value);
  std::string field_or(std::string_view key, std::string fallback) const;

  /// Deterministic single-line text encoding.
  std::string to_text() const;
};

/// Records an ordered evidence stream and derives a stable state digest over it.
class EvidenceRecorder {
public:
  EvidenceRecorder() = default;

  EvidenceId record(EvidenceKind kind, std::string phase,
                    std::map<std::string, std::string> fields = {},
                    std::string payload = {});

  void record_raw(EvidenceRecord ev);

  const std::vector<EvidenceRecord>& records() const noexcept { return records_; }
  /// Thread-safe snapshot (copy) of the recorded records.
  std::vector<EvidenceRecord> snapshot() const;
  bool empty() const noexcept;
  std::size_t size() const noexcept;
  std::int64_t next_seq() const noexcept;

  /// Deterministic text serialization.
  std::string serialize_text() const;

  /// Deterministic JSON serialization (stable key ordering).
  std::string serialize_json() const;

  /// Stable digest over the entire ordered evidence stream.
  Digest256 digest() const;

  /// Parse a text-serialized evidence stream back into records. Returns false on
  /// malformed input. Round-trips exactly with serialize_text().
  static bool parse_text(std::string_view text, std::vector<EvidenceRecord>& out);

private:
  std::vector<EvidenceRecord> records_;
  std::int64_t next_seq_{0};
  mutable std::mutex mtx_;   // makes concurrent recording + digest safe
};

} // namespace chaoslab
