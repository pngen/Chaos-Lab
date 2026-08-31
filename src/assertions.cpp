#include "chaoslab/assertions.h"

#include "chaoslab/text.h"

#include <array>

namespace chaoslab {

namespace {
constexpr std::array<const char*, static_cast<std::size_t>(AssertionKind::Last)> kKindNames = {
  "accepted","rejected","state","terminal","not_terminal","exactly_one_authority",
  "no_stale_mutation","no_double_commit","no_leak","accounting_zero",
  "digest_equal","digest_different","recovery_complete","process_exit",
  "process_alive","resource_baseline"
};
constexpr std::array<const char*, 4> kStatusNames = { "unevaluated","passed","failed","skipped" };
} // namespace

const char* assertion_kind_name(AssertionKind k) noexcept {
  auto i = static_cast<std::size_t>(k);
  return i < kKindNames.size() ? kKindNames[i] : "unknown";
}

const char* assertion_status_name(AssertionStatus s) noexcept {
  auto i = static_cast<std::size_t>(s);
  return i < kStatusNames.size() ? kStatusNames[i] : "unknown";
}

bool parse_assertion_kind(std::string_view s, AssertionKind& out) noexcept {
  std::string lower = to_lower(s);
  for (std::size_t i = 0; i < kKindNames.size(); ++i) {
    if (lower == kKindNames[i]) { out = static_cast<AssertionKind>(i); return true; }
  }
  return false;
}

void AssertionSpec::set_param(std::string key, std::string value) {
  params[std::move(key)] = std::move(value);
}

} // namespace chaoslab
