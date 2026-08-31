#pragma once
// Framed wire protocol for the Chaos Lab target runtime (coordinator/worker).
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/result.h"
#include "chaoslab/transport.h"   // Frame codec

#include <cstdint>
#include <string>

namespace chaoslab {
namespace proto {

// Frame type (type field of a Frame).
enum MsgType : std::uint32_t {
  REGISTER      = 0x0100,  // worker -> coordinator
  REGISTERED    = 0x0101,  // coordinator -> worker (ack, includes epoch)
  SUBMIT        = 0x0250,  // controller -> coordinator (dispatch request)
  SUBMIT_RESPONSE = 0x0251, // coordinator -> controller (assigned authority)
  DISPATCH      = 0x0200,  // coordinator -> worker
  WORK_STARTED  = 0x0201,  // worker -> coordinator
  COMPLETE      = 0x0300,  // worker -> coordinator
  COMPLETE_ACK  = 0x0301,  // coordinator -> worker (committed)
  COMPLETE_REJECTED = 0x0302, // coordinator -> worker (stale/duplicate)
  EPOCH_NEW     = 0x0400,  // coordinator -> worker (epoch changed)
  PING          = 0x0500,
  PONG          = 0x0501,
  STALE         = 0x0600,  // a fault-injected stale-frame marker
  GET_STATE     = 0x0800,
  STATE         = 0x0801,
  SHUTDOWN      = 0x0700
};

/// Send a frame with an optional text payload.
Status send_frame(int fd, std::uint32_t type, const std::string& payload = {});

/// Receive one framed message (blocking with timeout). Returns ok on success;
/// the payload is filled.
Status recv_frame(int fd, std::uint32_t& type, std::string& payload);

/// Get an ephemeral loopback port to launch the coordinator on.
Status pick_ephemeral_port(std::uint16_t& port);

/// A tiny big-endian u64 write/read helper for payloads that embed counters.
void put_u64(std::string& out, std::uint64_t v);
bool get_u64(const std::string& in, std::size_t off, std::uint64_t& out);

} // namespace proto
} // namespace chaoslab
