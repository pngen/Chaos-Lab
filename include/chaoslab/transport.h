#pragma once
// Faultable framed-TCP transport interposer.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/result.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace chaoslab {

/// A single framed message (magic | type | length | payload).
struct Frame {
  std::uint32_t type{0};
  std::vector<std::uint8_t> payload;

  static constexpr std::uint32_t MAGIC = 0x4348414fu; // 'CHAO'
  static constexpr std::size_t HEADER = 12;

  std::vector<std::uint8_t> encode() const;
  /// Decodes one frame from a buffer. On success sets out and consumed; returns
  /// true. Returns false when the buffer does not yet hold a complete frame.
  static bool decode(const std::uint8_t* src, std::size_t n, Frame& out, std::size_t& consumed);
  /// Parse just the 12-byte header (magic, type, length) without requiring the
  /// payload, so stream readers can size the body first.
  static bool decode_header(const std::uint8_t* src, std::size_t n, std::uint32_t& type, std::uint32_t& len);
};

enum class Direction { C2S, S2C };
const char* direction_name(Direction d) noexcept;

enum class TransportFaultKind { TF_DROP, TF_DUPLICATE, TF_TRUNCATE, TF_CORRUPT, TF_REORDER, TF_DELAY, TF_HALF_CLOSE, TF_CLOSE };
const char* transport_fault_name(TransportFaultKind k) noexcept;

/// A fault applied to a frame stream. Targeted by direction, message type and
/// per-direction frame sequence so unrelated traffic is never touched.
struct TransportFault {
  TransportFaultKind kind{TransportFaultKind::TF_DROP};
  Direction direction{Direction::C2S};
  std::uint32_t message_type{0}; ///< 0 = any
  std::uint64_t at_sequence{0};  ///< 1-based per-direction frame index; 0 = any
  int offset{0};                 ///< truncation/corruption byte offset
};

/// Statistics exposed by a run for evidence.
struct TransportStats {
  std::uint64_t frames_forwarded{0};
  std::uint64_t faults_applied{0};
  std::uint64_t dropped{0};
  std::uint64_t truncated{0};
  std::uint64_t corrupted{0};
  std::uint64_t duplicated{0};
  std::uint64_t delayed{0};
  std::uint64_t reordered{0};
};

/// A loopback TCP proxy with deterministic fault injection on a frame stream.
/// It listens on a chosen port and forwards to a target, applying the ordered
/// fault list. All proxy state is external to any target runtime.
class FaultyTransport {
public:
  FaultyTransport() = default;
  ~FaultyTransport();
  FaultyTransport(const FaultyTransport&) = delete;
  FaultyTransport& operator=(const FaultyTransport&) = delete;

  Status start(std::uint16_t listen_port, const std::string& target_host, std::uint16_t target_port);
  void stop() noexcept;

  /// Add a fault to the ordered list (applied in order of insertion).
  void enqueue_fault(TransportFault f);

  std::uint16_t listen_port() const noexcept { return listen_port_; }
  bool running() const noexcept { return running_.load(); }

  const TransportStats& stats() const noexcept { return stats_; }

private:
  void accept_loop();
  void run_connection(int client_fd, int target_fd);

  std::atomic<bool> running_{false};
  std::atomic<int> server_fd_{-1};
  std::string target_host_;
  std::uint16_t target_port_{0};
  std::uint16_t listen_port_{0};
  std::thread accept_thread_;
  std::vector<TransportFault> faults_;
  TransportStats stats_;
};

} // namespace chaoslab
