#include "chaoslab/text.h"

#include <cctype>
#include <cstdio>

namespace chaoslab {

std::string to_hex(std::uint64_t value) noexcept {
  static const char* d = "0123456789abcdef";
  std::string s(16, '0');
  for (int i = 15; i >= 0; --i) {
    s[static_cast<std::size_t>(i)] = d[value & 0x0f];
    value >>= 4;
  }
  return s;
}

bool from_hex(std::string_view s, std::uint64_t& out) noexcept {
  if (s.empty()) return false;
  std::uint64_t v = 0;
  for (char c : s) {
    int nib = -1;
    if (c >= '0' && c <= '9') nib = c - '0';
    else if (c >= 'a' && c <= 'f') nib = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') nib = c - 'A' + 10;
    if (nib < 0) return false;
    v = (v << 4) | static_cast<std::uint64_t>(nib);
  }
  out = v;
  return true;
}

std::vector<std::string> split(std::string_view s, char delim) {
  std::vector<std::string> out;
  std::size_t start = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == delim) {
      out.emplace_back(s.substr(start, i - start));
      start = i + 1;
    }
  }
  out.emplace_back(s.substr(start));
  return out;
}

std::string join(const std::vector<std::string>& parts, std::string_view delim) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) out.append(delim);
    out.append(parts[i]);
  }
  return out;
}

std::string hex0x(std::uint64_t value) noexcept { return "0x" + to_hex(value); }

bool starts_with(std::string_view s, std::string_view prefix) noexcept {
  return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
}

bool ends_with(std::string_view s, std::string_view suffix) noexcept {
  return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix;
}

std::string trim(std::string_view s) {
  std::size_t b = 0;
  std::size_t e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

std::string to_lower(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (char c : s) out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  return out;
}

std::string to_string_hex(std::uint64_t v) noexcept {
  return to_hex(v);
}

} // namespace chaoslab
