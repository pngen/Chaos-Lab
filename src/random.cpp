#include "chaoslab/random.h"

#include "chaoslab/digest.h"

namespace chaoslab {

std::uint64_t SplitMix64::uniform(std::uint64_t lo, std::uint64_t hi) noexcept {
  if (hi <= lo) return lo;
  std::uint64_t span = hi - lo + 1;
  return lo + (next() % span);
}

std::uint64_t SplitMix64::below(std::uint64_t n) noexcept {
  if (n == 0) return 0;
  return next() % n;
}

std::vector<std::uint8_t> SplitMix64::bytes(std::size_t n) noexcept {
  std::vector<std::uint8_t> out;
  out.reserve(n);
  for (std::size_t i = 0; i < n; ++i) out.push_back(static_cast<std::uint8_t>(next()));
  return out;
}

std::uint64_t derive_subseed(std::uint64_t seed, const char* key) noexcept {
  // SHA-256 over (key || seed) folded to a u64 — deterministic and key-distinct.
  Sha256 h;
  if (key != nullptr) {
    h.update(key);
  }
  std::uint8_t b[8];
  for (int i = 0; i < 8; ++i) b[i] = static_cast<std::uint8_t>(seed >> (56 - 8 * i));
  h.update(b, 8);
  Digest256 d = h.finish();
  return d.to_u64(0) ^ d.to_u64(1) ^ d.to_u64(2) ^ d.to_u64(3);
}

} // namespace chaoslab
