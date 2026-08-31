#pragma once
// SHA-256 state digest for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace chaoslab {

/// A 256-bit digest, represented as 32 bytes.
class Digest256 {
public:
  Digest256() noexcept = default;

  /// Construct from 32 raw bytes.
  static Digest256 from_bytes(const std::uint8_t bytes[32]) noexcept;

  /// Parse a lowercase hex digest (exact 64 hex characters). Returns false on error.
  static bool parse(std::string_view hex, Digest256& out) noexcept;

  /// Compute the SHA-256 digest of a message.
  static Digest256 sha256(std::string_view message) noexcept;

  /// The raw 32-byte digest.
  const std::uint8_t* data() const noexcept { return data_; }
  std::uint8_t* data() noexcept { return data_; }
  std::size_t size() const noexcept { return 32; }

  /// Lowercase hex encoding (64 characters).
  std::string hex() const noexcept;

  /// Equality comparison.
  friend bool operator==(const Digest256& a, const Digest256& b) noexcept {
    return a.to_u64(0) == b.to_u64(0) && a.to_u64(1) == b.to_u64(1) &&
           a.to_u64(2) == b.to_u64(2) && a.to_u64(3) == b.to_u64(3);
  }
  friend bool operator!=(const Digest256& a, const Digest256& b) noexcept {
    return !(a == b);
  }

  /// Interpret a consecutive 8-byte span as a big-endian u64 (words 0..3).
  std::uint64_t to_u64(int word) const noexcept;

private:
  std::uint8_t data_[32]{};
};

/// Streaming SHA-256 (incremental) so large state can be hashed without buffering.
class Sha256 {
public:
  Sha256() noexcept { reset(); }
  void reset() noexcept;
  void update(const std::uint8_t* data, std::size_t len) noexcept;
  void update(std::string_view message) noexcept { update(reinterpret_cast<const std::uint8_t*>(message.data()), message.size()); }
  Digest256 finish() noexcept;

private:
  void process_block(const std::uint8_t block[64]) noexcept;

  std::uint32_t state_[8];
  std::uint64_t total_;
  std::uint8_t buffer_[64];
  std::size_t buffered_;
};

} // namespace chaoslab
