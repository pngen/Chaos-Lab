#include "chaoslab/windows_error.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cstring>
#endif

namespace chaoslab {

std::string last_error_message(unsigned long code) {
#ifdef _WIN32
  if (code == 0) code = ::GetLastError();
  if (code == 0) return "no error";
  char buf[512] = {0};
  DWORD n = ::FormatMessageA(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, code, 0, buf, static_cast<DWORD>(sizeof buf), nullptr);
  std::string s = n ? buf : "";
  while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
  if (s.empty()) s = "Win32 error " + std::to_string(code);
  return s;
#else
  return std::strerror(static_cast<int>(code));
#endif
}

} // namespace chaoslab
