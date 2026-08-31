#include "chaoslab/result.h"

namespace chaoslab {

const char* to_string(StatusCode code) noexcept {
  switch (code) {
    case StatusCode::ok: return "ok";
    case StatusCode::invalid_argument: return "invalid_argument";
    case StatusCode::already_exists: return "already_exists";
    case StatusCode::not_found: return "not_found";
    case StatusCode::out_of_bounds: return "out_of_bounds";
    case StatusCode::permission_denied: return "permission_denied";
    case StatusCode::not_supported: return "not_supported";
    case StatusCode::io_error: return "io_error";
    case StatusCode::protocol_error: return "protocol_error";
    case StatusCode::authority_rejected: return "authority_rejected";
    case StatusCode::campaign_closed: return "campaign_closed";
    case StatusCode::resource_limit: return "resource_limit";
    case StatusCode::process_error: return "process_error";
    case StatusCode::internal_error: return "internal_error";
  }
  return "unknown";
}

std::string Status::to_string() const {
  std::string s = chaoslab::to_string(code_);
  if (!message_.empty()) {
    s += ": ";
    s += message_;
  }
  return s;
}

} // namespace chaoslab
