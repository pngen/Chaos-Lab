#pragma once
// Windows process-control layer for Chaos Lab.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/result.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace chaoslab {

/// A launched child process. Owns its OS handle and stdout/stderr pipes. Only
/// processes launched by this layer may be killed/terminated by a campaign.
class Process {
public:
  Process() = default;
  ~Process();
  Process(const Process&) = delete;
  Process& operator=(const Process&) = delete;
  Process(Process&& other) noexcept;
  Process& operator=(Process&& other) noexcept;

  /// Launch a process with optional working directory, environment overrides
  /// (overlaid on the parent environment) and stdout/stderr capture.
  static Status launch(const std::string& executable,
                       const std::vector<std::string>& arguments,
                       const std::string& working_dir,
                       const std::map<std::string, std::string>& environment,
                       Process& out);

  bool valid() const noexcept { return process_ != nullptr; }

  /// Non-blocking liveness check.
  bool alive();

  std::uint64_t pid() const noexcept { return pid_; }

  /// Graceful shutdown (WM_CLOSE) where possible; on the target we use a
  /// best-effort graceful signal then the caller may force kill.
  Status terminate(bool graceful);

  /// Block until the process exits (or a bounded wait in ms, 0 = non-blocking).
  Status wait(std::uint32_t timeout_ms);

  /// Whether the process has already been collected as exited.
  bool exited() const noexcept { return exited_; }

  int exit_code() const noexcept { return exit_code_; }

  /// Non-blocking drain of captured stdout / stderr. Returns new bytes.
  std::string read_stdout();
  std::string read_stderr();

  void close() noexcept;

private:
  void* process_ = nullptr;   // HANDLE
  std::uint64_t pid_{0};
  void* pipe_out_ = nullptr;  // HANDLE (read end in parent)
  void* pipe_err_ = nullptr;
  bool exited_{false};
  int exit_code_{0};
};

} // namespace chaoslab
