# Contributing to Chaos Lab

Chaos Lab accepts contributions from individuals and organizations on the terms
of the Apache License 2.0. No Contributor License Agreement (CLA) is required.

## License and contributions

By submitting a contribution, you agree that it is licensed to the project under
the Apache License, Version 2.0, and that you have the right to make that grant.
See the root LICENSE for the full license text and NOTICE for the project notice.

## Build and test

Chaos Lab is C++20 + CMake, Windows-first, built with MSVC at /W4 /WX.

Configure and build (a valid CUDA toolchain enables CUDA scenarios):

    cmake -S . -B build -G Ninja -DCMAKE_CXX_COMPILER=cl -DCMAKE_CUDA_COMPILER=nvcc
    cmake --build build

Run the test suite:

    ctest --test-dir build --output-on-failure

## Code-quality expectations

- Zero compiler warnings under /W4 /WX (MSVC) and equivalent strict flags.
- Deterministic behavior: any new randomness must derive from the campaign seed.
- Fault scenarios must remain bounded and deterministic. No infinite loops, no
  arbitrary long sleeps where an event/state barrier is possible, and no
  dependence on wall-clock timing for correctness.
- Destructive tests must operate only against campaign-owned resources (owned
  processes, campaign workspace files, governed resource caps). Never kill
  arbitrary machine processes, mutate arbitrary user files, or touch system
  driver/GPU state.
- Every injected fault must be recorded as evidence with its source, scope,
  severity, timing and expected effect.

## Test policy

- No test timeouts, watchdogs, or process time limits. Tests must complete on
  their own or be terminated only after diagnosing a genuine hang.
- Deterministic property coverage is welcome: fixed-seed randomized tests must
  remain reproducible from the seed.
- No OCR or screenshot-based validation.
