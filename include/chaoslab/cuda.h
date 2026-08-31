#pragma once
// CUDA-backed chaos scenario primitives (real RTX 5090 / sm_120 where present).
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/result.h"

#include <cstdint>
#include <string>
#include <vector>

#ifdef CHAOSLAB_HAS_CUDA
extern "C" {
/// Launch the real bounded SAXPY kernel. Returns 0 on success.
int chaoslab_saxpy(unsigned long long n, double a, const double* x, double* y);
}
#endif

namespace chaoslab {

/// Real device info. All calls are no-ops (empty results) when no CUDA device
/// is present, so the library still links and runs without a GPU.
struct DeviceInfo {
  std::uint64_t total_memory_bytes{0};
  std::uint64_t free_memory_bytes{0};
  int compute_capability_major{0};
  int compute_capability_minor{0};
  std::string name;
};

/// Query the CUDA device. Returns ok even if no device (name empty).
Status cuda_query_device(DeviceInfo& out);

/// RAII handle to a real device buffer.
class DeviceBuffer {
public:
  DeviceBuffer() = default;
  ~DeviceBuffer();
  DeviceBuffer(const DeviceBuffer&) = delete;
  DeviceBuffer& operator=(const DeviceBuffer&) = delete;
  DeviceBuffer(DeviceBuffer&& other) noexcept;
  DeviceBuffer& operator=(DeviceBuffer&& other) noexcept;

  /// Allocate p bytes of device memory, bounded by p max_bytes (0 = unbounded
  /// governed by a sane safety cap). The request may fail if it exceeds the
  /// governed cap or the device cannot satisfy it.
  Status allocate(std::size_t bytes, std::uint64_t max_bytes = 0);
  void release() noexcept;
  std::size_t size() const noexcept { return bytes_; }
  void* data() const noexcept { return ptr_; }
  void* data() noexcept { return ptr_; }

private:
  void* ptr_ = nullptr;
  std::size_t bytes_ = 0;
};

/// A real bounded kernel: y = a*x + y on N doubles.
Status cuda_saxpy(std::size_t n, double a, const double* x, double* y);

/// H2D copy into a device buffer.
Status cuda_h2d(void* dst_device, const void* src_host, std::size_t bytes);

/// D2H copy out of a device buffer.
Status cuda_d2h(void* dst_host, const void* src_device, std::size_t bytes);

/// Drive device allocation pressure up to target_bytes (bounded by cap_bytes).
/// Lives in the owning process; the caller releases all buffers afterwards.
Status cuda_device_pressure(std::uint64_t target_bytes, std::uint64_t cap_bytes,
                            std::vector<DeviceBuffer>& out, std::uint64_t& total);

} // namespace chaoslab
