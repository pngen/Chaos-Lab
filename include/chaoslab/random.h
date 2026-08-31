#pragma once
// Deterministic pseudo-random generators for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include <cstdint>
#include <vector>

namespace chaoslab {

/// A deterministic splitmix64 stream. Given the same seed it always yields the
/// same sequence, so campaigns are reproducible from their seed alone.
class SplitMix64 {
public:
  explicit SplitMix64(std::uint64_t seed) noexcept : state_(seed) {}

  std::uint64_t next() noexcept {
    std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
  }

  std::uint64_t peek() const noexcept {
    std::uint64_t z = (state_ + 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
  }

  std::uint64_t state() const noexcept { return state_; }
  void resize(std::uint64_t s) noexcept { state_ = s; }

  /// Uniform integer in [lo, hi].
  std::uint64_t uniform(std::uint64_t lo, std::uint64_t hi) noexcept;

  /// Uniform integer in [0, n); n must be > 0.
  std::uint64_t below(std::uint64_t n) noexcept;

  /// Fill a vector member with n uniform bytes.
  std::vector<std::uint8_t> bytes(std::size_t n) noexcept;

  void reseed(std::uint64_t seed) noexcept { state_ = seed; }

private:
  std::uint64_t state_;
};

/// Derive a deterministic per-component sub-seed from a campaign seed and a key.
std::uint64_t derive_subseed(std::uint64_t seed, const char* key) noexcept;

/// Fixed, non-zero default campaign seed. All randomness derives from this.
inline constexpr std::uint64_t default_seed() noexcept { return 0xCA05C0DE5EED07ULL; }

} // namespace chaoslab
