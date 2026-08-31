#include "chaoslab/cuda.h"

#ifdef CHAOSLAB_HAS_CUDA
#include <cuda_runtime.h>
#endif

#include <cstdint>
#include <vector>

namespace chaoslab {

#ifdef CHAOSLAB_HAS_CUDA

Status cuda_query_device(DeviceInfo& out) {
  int count = 0;
  cudaError_t e = cudaGetDeviceCount(&count);
  if (e != cudaSuccess || count <= 0) {
    out = DeviceInfo{};
    return Status::ok(); // no device; not an error
  }
  cudaDeviceProp prop;
  e = cudaGetDeviceProperties(&prop, 0);
  if (e != cudaSuccess) return Status::error(StatusCode::internal_error, "cudaGetDeviceProperties");
  out.total_memory_bytes = prop.totalGlobalMem;
  out.compute_capability_major = prop.major;
  out.compute_capability_minor = prop.minor;
  out.name = prop.name;
  std::size_t free_b = 0, total_b = 0;
  if (cudaMemGetInfo(&free_b, &total_b) == cudaSuccess) out.free_memory_bytes = free_b;
  return Status::ok();
}

DeviceBuffer::~DeviceBuffer() { release(); }
DeviceBuffer::DeviceBuffer(DeviceBuffer&& other) noexcept { ptr_ = other.ptr_; bytes_ = other.bytes_; other.ptr_ = nullptr; other.bytes_ = 0; }
DeviceBuffer& DeviceBuffer::operator=(DeviceBuffer&& other) noexcept {
  if (this != &other) { release(); ptr_ = other.ptr_; bytes_ = other.bytes_; other.ptr_ = nullptr; other.bytes_ = 0; }
  return *this;
}
Status DeviceBuffer::allocate(std::size_t bytes, std::uint64_t max_bytes) {
  if (max_bytes != 0 && bytes > max_bytes)
    return Status::error(StatusCode::resource_limit, "allocate exceeds device cap");
  if (ptr_) release();
  if (bytes == 0) return Status::ok();
  void* p = nullptr;
  cudaError_t e = cudaMalloc(&p, bytes);
  if (e != cudaSuccess)
    return Status::error(StatusCode::resource_limit, "cudaMalloc failed: " + std::string(cudaGetErrorString(e)));
  ptr_ = p; bytes_ = bytes;
  return Status::ok();
}
void DeviceBuffer::release() noexcept {
  if (ptr_) { cudaFree(ptr_); ptr_ = nullptr; bytes_ = 0; }
}

Status cuda_saxpy(std::size_t n, double a, const double* x, double* y) {
  int rc = chaoslab_saxpy(static_cast<unsigned long long>(n), a, x, y);
  if (rc != 0) {
    return Status::error(StatusCode::internal_error, "SAXPY kernel failed");
  }
  return Status::ok();
}

Status cuda_h2d(void* dst_device, const void* src_host, std::size_t bytes) {
  cudaError_t e = cudaMemcpy(dst_device, src_host, bytes, cudaMemcpyHostToDevice);
  return e == cudaSuccess ? Status::ok()
    : Status::error(StatusCode::internal_error, std::string("cudaMemcpy H2D: ") + cudaGetErrorString(e));
}
Status cuda_d2h(void* dst_host, const void* src_device, std::size_t bytes) {
  cudaError_t e = cudaMemcpy(dst_host, src_device, bytes, cudaMemcpyDeviceToHost);
  return e == cudaSuccess ? Status::ok()
    : Status::error(StatusCode::internal_error, std::string("cudaMemcpy D2H: ") + cudaGetErrorString(e));
}

Status cuda_device_pressure(std::uint64_t target_bytes, std::uint64_t cap_bytes,
                            std::vector<DeviceBuffer>& out, std::uint64_t& total) {
  out.clear();
  total = 0;
  // Respect a sensible internal cap so we never try to exhaust the whole GPU.
  constexpr std::uint64_t kChunk = 256ULL * 1024 * 1024;
  while (total < target_bytes) {
    std::uint64_t step = kChunk;
    if (cap_bytes != 0 && total + step > cap_bytes) {
      step = cap_bytes - total;
      if (step < 4ULL * 1024 * 1024) break;
    }
    if (total + step > target_bytes) step = target_bytes - total;
    DeviceBuffer b;
    if (b.allocate(static_cast<std::size_t>(step), cap_bytes).failed()) break;
    total += step;
    out.push_back(std::move(b));
    if (cap_bytes != 0 && total >= cap_bytes) break;
  }
  return Status::ok();
}

#else // no CUDA

Status cuda_query_device(DeviceInfo& out) { out = DeviceInfo{}; return Status::ok(); }
DeviceBuffer::~DeviceBuffer() {}
DeviceBuffer::DeviceBuffer(DeviceBuffer&& other) noexcept { ptr_ = other.ptr_; bytes_ = other.bytes_; other.ptr_=nullptr; other.bytes_=0; }
DeviceBuffer& DeviceBuffer::operator=(DeviceBuffer&& other) noexcept { if (this != &other) { ptr_=other.ptr_; bytes_=other.bytes_; other.ptr_=nullptr; other.bytes_=0; } return *this; }
Status DeviceBuffer::allocate(std::size_t bytes, std::uint64_t) { if (bytes==0) return Status::ok(); return Status::error(StatusCode::not_supported, "CUDA not available"); }
void DeviceBuffer::release() noexcept {}
Status cuda_saxpy(std::size_t, double, const double*, double*) { return Status::error(StatusCode::not_supported, "CUDA not available"); }
Status cuda_h2d(void*, const void*, std::size_t) { return Status::error(StatusCode::not_supported, "CUDA not available"); }
Status cuda_d2h(void*, const void*, std::size_t) { return Status::error(StatusCode::not_supported, "CUDA not available"); }
Status cuda_device_pressure(std::uint64_t, std::uint64_t, std::vector<DeviceBuffer>&, std::uint64_t&) { return Status::error(StatusCode::not_supported, "CUDA not available"); }

#endif

} // namespace chaoslab
