#include "chaoslab/resource.h"

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
