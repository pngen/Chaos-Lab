#pragma once
// Text helpers for deterministic serialization and formatting.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chaoslab {

std::string to_hex(std::uint64_t value) noexcept;
bool from_hex(std::string_view s, std::uint64_t& out) noexcept;

/// Split a string by a delimiter, preserving empty segments.
std::vector<std::string> split(std::string_view s, char delim);

/// Join strings with a delimiter.
std::string join(const std::vector<std::string>& parts, std::string_view delim);

/// Format a std::string_view as printf-style hex with a 0x prefix.
std::string hex0x(std::uint64_t value) noexcept;

bool starts_with(std::string_view s, std::string_view prefix) noexcept;
bool ends_with(std::string_view s, std::string_view suffix) noexcept;

/// Trim ASCII whitespace from both ends.
std::string trim(std::string_view s);

/// Lowercase a string (ASCII).
std::string to_lower(std::string_view s);

/// Formatted concatenation of arguments using std::to_string-like conversion.
template <typename... Args>
std::string fmt(Args&&... args) {
  std::string out;
  (out.append(std::forward<Args>(args)), ...);
  return out;
}

/// printf-style formatting for arithmetic values into a hex string.
std::string to_string_hex(std::uint64_t v) noexcept;

} // namespace chaoslab
