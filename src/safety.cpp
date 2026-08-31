#include "chaoslab/safety.h"

#include "chaoslab/text.h"

#include <algorithm>

namespace chaoslab {

Status SafetyEnvelope::allow_executable(const std::string& path) const {
  if (env_.allowed_executables.empty()) return Status::ok();
  std::string lower = to_lower(path);
  for (auto& a : env_.allowed_executables) {
    if (to_lower(a) == lower) return Status::ok();
  }
  return Status::error(StatusCode::permission_denied,
                       "executable not in allowed list: " + path);
}

Status SafetyEnvelope::allow_target(TargetId id) const {
  if (is_owned_target(id)) return Status::ok();
  return Status::error(StatusCode::permission_denied,
                       "target not owned by campaign: " + id.str());
}

Status SafetyEnvelope::allow_path(const std::string& path) const {
  if (env_.allowed_temp_root.empty()) return Status::ok();
  if (starts_with(path, env_.allowed_temp_root)) return Status::ok();
  return Status::error(StatusCode::permission_denied,
                       "path outside allowed temp root: " + path);
}

Status SafetyEnvelope::allow_pid(std::uint64_t pid) const {
  if (env_.allowed_pids.empty()) return Status::ok();
  if (std::find(env_.allowed_pids.begin(), env_.allowed_pids.end(), pid) != env_.allowed_pids.end())
    return Status::ok();
  return Status::error(StatusCode::permission_denied,
                       "pid not in allowed set: " + std::to_string(pid));
}

Status SafetyEnvelope::acquire_child() {
  if (child_count_ >= env_.max_child_processes) {
    return Status::error(StatusCode::resource_limit,
                         "child process cap reached (" + std::to_string(env_.max_child_processes) + ")");
  }
  ++child_count_;
  if (child_count_ > peak_child_count_) peak_child_count_ = child_count_;
  return Status::ok();
}

void SafetyEnvelope::release_child() noexcept {
  if (child_count_ > 0) --child_count_;
}

bool SafetyEnvelope::can_spawn_more() const noexcept {
  return child_count_ < env_.max_child_processes;
}

void SafetyEnvelope::add_owned_target(TargetId id) { owned_.insert(id); }
bool SafetyEnvelope::is_owned_target(TargetId id) const noexcept { return owned_.count(id) != 0; }

Status SafetyEnvelope::acquire_socket() {
  if (socket_count_ >= env_.max_open_sockets) {
    return Status::error(StatusCode::resource_limit,
                         "open socket cap reached (" + std::to_string(env_.max_open_sockets) + ")");
  }
  ++socket_count_;
  if (socket_count_ > peak_socket_count_) peak_socket_count_ = socket_count_;
  return Status::ok();
}

void SafetyEnvelope::release_socket() noexcept {
  if (socket_count_ > 0) --socket_count_;
}

Status SafetyEnvelope::acquire_restart() {
  if (restart_count_ >= env_.max_restarts) {
    return Status::error(StatusCode::resource_limit,
                         "restart cap reached (" + std::to_string(env_.max_restarts) + ")");
  }
  ++restart_count_;
  return Status::ok();
}

int SafetyEnvelope::restart_budget_remaining() const noexcept {
  return env_.max_restarts > restart_count_ ? env_.max_restarts - restart_count_ : 0;
}

Status SafetyEnvelope::validate() const {
  if (!env_.validate()) {
    return Status::error(StatusCode::invalid_argument, "resource envelope invalid");
  }
  return Status::ok();
}

} // namespace chaoslab
