#include "chaoslab/resource.h"

#include <algorithm>
#include <cstdlib>

namespace chaoslab {

HostAllocation::~HostAllocation() { release(); }

HostAllocation::HostAllocation(HostAllocation&& other) noexcept {
  ptr_ = other.ptr_; bytes_ = other.bytes_;
  other.ptr_ = nullptr; other.bytes_ = 0;
}

HostAllocation& HostAllocation::operator=(HostAllocation&& other) noexcept {
  if (this != &other) {
    release();
    ptr_ = other.ptr_; bytes_ = other.bytes_;
    other.ptr_ = nullptr; other.bytes_ = 0;
  }
  return *this;
}

Status HostAllocation::allocate(std::size_t bytes, std::uint64_t max_bytes) {
  if (max_bytes != 0 && bytes > max_bytes) {
    return Status::error(StatusCode::resource_limit,
                         "allocation exceeds host cap: " + std::to_string(bytes) +
                         " > " + std::to_string(max_bytes));
  }
  if (ptr_) release();
  if (bytes == 0) return Status::ok();
  void* p = std::malloc(bytes);
  if (!p) return Status::error(StatusCode::resource_limit, "host allocation failed: " + std::to_string(bytes) + " bytes");
  ptr_ = p; bytes_ = bytes;
  volatile std::uint8_t* vp = static_cast<std::uint8_t*>(p);
  for (std::size_t i = 0; i < bytes; i += 4096) vp[i] = 0;
  return Status::ok();
}

void HostAllocation::release() noexcept {
  if (ptr_) { std::free(ptr_); ptr_ = nullptr; bytes_ = 0; }
}

Status ResourceGovernor::try_allocate(std::size_t bytes, std::size_t& allocated) {
  if (env_.max_host_allocation_bytes != 0 &&
      allocated_ + bytes > env_.max_host_allocation_bytes) {
    return Status::error(StatusCode::resource_limit, "host allocation cap exceeded");
  }
  if (bytes == 0) { allocated = 0; return Status::ok(); }
  void* p = std::malloc(bytes);
  if (!p) return Status::error(StatusCode::resource_limit, "allocation failed");
  std::free(p);
  allocated_ += bytes;
  allocated = bytes;
  return Status::ok();
}

void ResourceGovernor::account_free(std::size_t bytes) noexcept {
  allocated_ = bytes > allocated_ ? 0 : allocated_ - bytes;
}

Status ResourceGovernor::reserve(std::uint64_t units) {
  if (env_.max_host_allocation_bytes != 0 && reserved_ + units > env_.max_host_allocation_bytes) {
    return Status::error(StatusCode::resource_limit, "reservation cap exceeded");
  }
  reserved_ += units;
  return Status::ok();
}

void ResourceGovernor::release_reservation(std::uint64_t units) noexcept {
  reserved_ = units > reserved_ ? 0 : reserved_ - units;
}

std::string ResourceBaseline::delta_report() const {
  std::string s;
  auto line = [&](const char* k, std::uint64_t b, std::uint64_t p, std::uint64_t a) {
    s += std::string(k) + " before=" + std::to_string(b) + " peak=" + std::to_string(p) +
         " after=" + std::to_string(a) + " delta=" + std::to_string(a > b ? a - b : 0) + "\n";
  };
  line("child_processes", before.child_processes, peak.child_processes, after.child_processes);
  line("open_sockets", before.open_sockets, peak.open_sockets, after.open_sockets);
  line("host_bytes", before.host_bytes, peak.host_bytes, after.host_bytes);
  line("device_bytes", before.device_bytes, peak.device_bytes, after.device_bytes);
  line("temp_files", before.temp_files, peak.temp_files, after.temp_files);
  line("threads", before.threads, peak.threads, after.threads);
  if (!notes.empty()) { s += "notes:\n"; for (auto& n : notes) s += "  " + n + "\n"; }
  return s;
}

std::string ResourceBaseline::to_text() const {
  return std::string("leak=") + (leak ? "LEAK" : "clean") + " before=(" + std::to_string(before.child_processes) + "p," +
         std::to_string(before.host_bytes) + "hb," + std::to_string(before.device_bytes) + "db)" +
         " after=(" + std::to_string(after.child_processes) + "p," + std::to_string(after.host_bytes) + "hb," +
         std::to_string(after.device_bytes) + "db)";
}

void ResourceBaselineTracker::observe(const ResourceCounts& now) {
  if (!captured_) { baseline_.before = now; captured_ = true; }
  // Update after every call; the final call yields the post-cleanup value.
  baseline_.after = now;
  auto& pk = baseline_.peak;
  pk.child_processes = std::max(pk.child_processes, now.child_processes);
  pk.open_sockets = std::max(pk.open_sockets, now.open_sockets);
  pk.host_bytes = std::max(pk.host_bytes, now.host_bytes);
  pk.device_bytes = std::max(pk.device_bytes, now.device_bytes);
  pk.temp_files = std::max(pk.temp_files, now.temp_files);
  pk.threads = std::max(pk.threads, now.threads);
  // Leak verdict: after has returned to (or is below) before for owned resources.
  // A nonzero after for an owned category that was zero before is a leak.
  auto leaked = [&](std::uint64_t b, std::uint64_t a) { return a > b && a != 0; };
  baseline_.leak = leaked(baseline_.before.child_processes, now.child_processes) ||
                   leaked(baseline_.before.open_sockets, now.open_sockets) ||
                   leaked(baseline_.before.host_bytes, now.host_bytes) ||
                   leaked(baseline_.before.temp_files, now.temp_files) ||
                   leaked(baseline_.before.threads, now.threads) ||
                   leaked(baseline_.before.device_bytes, now.device_bytes);
}

Status apply_host_pressure(std::uint64_t target_bytes, std::uint64_t cap_bytes,
                           std::vector<HostAllocation>& out, std::uint64_t& total) {
  out.clear();
  total = 0;
  constexpr std::size_t kChunk = 16 * 1024 * 1024;
  while (total < target_bytes) {
    std::size_t step = kChunk;
    if (cap_bytes != 0 && total + step > cap_bytes) {
      step = static_cast<std::size_t>(cap_bytes - total);
      if (step == 0) break;
    }
    if (total + step > target_bytes) step = static_cast<std::size_t>(target_bytes - total);
    HostAllocation a;
    if (a.allocate(step, cap_bytes).failed()) break;
    total += step;
    out.push_back(std::move(a));
    if (cap_bytes != 0 && total >= cap_bytes) break;
  }
  return Status::ok();
}

} // namespace chaoslab
