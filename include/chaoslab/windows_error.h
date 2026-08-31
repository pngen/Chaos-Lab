#pragma once
// Windows error formatting utilities.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include <string>

namespace chaoslab {

/// Get a human-readable message for the last Win32 error code.
std::string last_error_message(unsigned long code = 0);

} // namespace chaoslab
