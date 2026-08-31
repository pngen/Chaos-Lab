#include "chaoslab/digest.h"

#include <cstring>
#include <vector>

namespace chaoslab {
namespace {

inline std::uint32_t rotr(std::uint32_t x, int n) noexcept {
  return (x >> n) | (x << (32 - n));
}

constexpr std::uint32_t k[64] = {
  0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
  0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
  0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
  0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
  0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
  0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
  0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
  0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

} // namespace

Digest256 Digest256::from_bytes(const std::uint8_t bytes[32]) noexcept {
  Digest256 d;
  std::memcpy(d.data_, bytes, 32);
  return d;
}

bool Digest256::parse(std::string_view hex, Digest256& out) noexcept {
  if (hex.size() != 64) return false;
  std::uint8_t b[32]{};
  for (std::size_t i = 0; i < 32; ++i) {
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int hi = nib(hex[i * 2]);
    int lo = nib(hex[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    b[i] = static_cast<std::uint8_t>((hi << 4) | lo);
  }
  out = from_bytes(b);
  return true;
}

std::string Digest256::hex() const noexcept {
  static const char* d = "0123456789abcdef";
  std::string s;
  s.reserve(64);
  for (std::size_t i = 0; i < 32; ++i) {
    s.push_back(d[data_[i] >> 4]);
    s.push_back(d[data_[i] & 0x0f]);
  }
  return s;
}

std::uint64_t Digest256::to_u64(int word) const noexcept {
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) {
    v = (v << 8) | data_[word * 8 + i];
  }
  return v;
}

Digest256 Digest256::sha256(std::string_view message) noexcept {
  Sha256 h;
  h.update(message);
  return h.finish();
}

void Sha256::reset() noexcept {
  state_[0] = 0x6a09e667u; state_[1] = 0xbb67ae85u;
  state_[2] = 0x3c6ef372u; state_[3] = 0xa54ff53au;
  state_[4] = 0x510e527fu; state_[5] = 0x9b05688cu;
  state_[6] = 0x1f83d9abu; state_[7] = 0x5be0cd19u;
  total_ = 0;
  buffered_ = 0;
}

void Sha256::process_block(const std::uint8_t block[64]) noexcept {
  std::uint32_t w[64];
  for (int i = 0; i < 16; ++i) {
    w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
           (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
           (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
           (static_cast<std::uint32_t>(block[i * 4 + 3]));
  }
  for (int i = 16; i < 64; ++i) {
    std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
  std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
  for (int i = 0; i < 64; ++i) {
    std::uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
    std::uint32_t ch = (e & f) ^ ((~e) & g);
    std::uint32_t temp1 = h + S1 + ch + k[i] + w[i];
    std::uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
    std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    std::uint32_t temp2 = S0 + maj;
    h = g; g = f; f = e; e = d + temp1;
    d = c; c = b; b = a; a = temp1 + temp2;
  }
  state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
  state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const std::uint8_t* data, std::size_t len) noexcept {
  total_ += len;
  while (len > 0) {
    std::size_t take = 64 - buffered_;
    if (take > len) take = len;
    std::memcpy(buffer_ + buffered_, data, take);
    buffered_ += take;
    data += take;
    len -= take;
    if (buffered_ == 64) {
      process_block(buffer_);
      buffered_ = 0;
    }
  }
}

Digest256 Sha256::finish() noexcept {
  std::uint64_t bit_len = total_ * 8;
  std::uint8_t pad = 0x80;
  update(&pad, 1);
  std::uint8_t zero = 0;
  while (buffered_ != 56) update(&zero, 1);
  std::uint8_t len_be[8];
  for (int i = 0; i < 8; ++i) {
    len_be[i] = static_cast<std::uint8_t>(bit_len >> (56 - 8 * i));
  }
  update(len_be, 8);
  Digest256 d;
  for (int i = 0; i < 8; ++i) {
    d.data()[i * 4 + 0] = static_cast<std::uint8_t>(state_[i] >> 24);
    d.data()[i * 4 + 1] = static_cast<std::uint8_t>(state_[i] >> 16);
    d.data()[i * 4 + 2] = static_cast<std::uint8_t>(state_[i] >> 8);
    d.data()[i * 4 + 3] = static_cast<std::uint8_t>(state_[i]);
  }
  return d;
}

} // namespace chaoslab
