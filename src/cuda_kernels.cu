// Real CUDA kernel for Chaos Lab scenarios (RTX 5090 / sm_120).
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include <cuda_runtime.h>

__global__ void chaoslab_saxpy_kernel(double a, const double* x, double* y, unsigned long long n) {
  unsigned long long i = (unsigned long long)blockIdx.x * (unsigned long long)blockDim.x
                       + (unsigned long long)threadIdx.x;
  if (i < n) y[i] = a * x[i] + y[i];
}

extern "C" int chaoslab_saxpy(unsigned long long n, double a, const double* x, double* y) {
  if (n == 0) return 0;
  int block = 256;
  unsigned long long grid = (n + block - 1) / (unsigned long long)block;
  chaoslab_saxpy_kernel<<<(unsigned int)grid, block>>>(a, x, y, n);
  cudaError_t e = cudaGetLastError();
  if (e != cudaSuccess) return (int)e;
  e = cudaDeviceSynchronize();
  return (int)e;
}
