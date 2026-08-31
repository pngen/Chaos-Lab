#pragma once
// Lightweight status/result types for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include <string>
#include <utility>

namespace chaoslab {

enum class StatusCode {
  ok = 0,
  invalid_argument,
  already_exists,
  not_found,
  out_of_bounds,
  permission_denied,
  not_supported,
  io_error,
  protocol_error,
  authority_rejected,
  campaign_closed,
  resource_limit,
  process_error,
  internal_error,
};

const char* to_string(StatusCode code) noexcept;

/// A simple status with an optional message.
class Status {
public:
  Status() noexcept : code_(StatusCode::ok) {}
  Status(StatusCode code, std::string message = {}) : code_(code), message_(std::move(message)) {}

  static Status ok() noexcept { return Status(); }
  static Status error(StatusCode code, std::string message) {
    return Status(code, std::move(message));
  }

  bool okay() const noexcept { return code_ == StatusCode::ok; }
  bool failed() const noexcept { return !okay(); }
  StatusCode code() const noexcept { return code_; }
  const std::string& message() const noexcept { return message_; }

  /// Human readable string.
  std::string to_string() const;

private:
  StatusCode code_;
  std::string message_;
};

} // namespace chaoslab
