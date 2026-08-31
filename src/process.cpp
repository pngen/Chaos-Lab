#include "chaoslab/process.h"

#include "chaoslab/windows_error.h"

#ifdef _WIN32

#include <algorithm>
#include <cwchar>
#include <sstream>

namespace chaoslab {

namespace {

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

// Quote a single command-line argument for CommandLineToArgvW semantics.
std::string quote_arg(const std::string& a) {
  if (a.empty()) return "\"\"";
  bool need = a.find_first_of(" \t\"") != std::string::npos;
  if (!need) return a;
  std::string out = "\"";
  for (char c : a) {
    if (c == '"') out += "\\\"";
    else out.push_back(c);
  }
  out += "\"";
  return out;
}

std::string build_command_line(const std::string& exe, const std::vector<std::string>& args) {
  std::string cmd = quote_arg(exe);
  for (auto& a : args) { cmd += ' '; cmd += quote_arg(a); }
  return cmd;
}

// Build an environment block: current environment overlaid with overrides.
std::wstring build_environment_block(const std::map<std::string, std::string>& overrides) {
  // Start from the current process environment.
  wchar_t* cur = GetEnvironmentStringsW();
  std::wstring blk;
  if (cur) {
    const wchar_t* p = cur;
    while (*p) {
      std::wstring entry(p);
      // Override if key matches.
      std::size_t eq = entry.find(L'=');
      bool replaced = false;
      if (eq != std::wstring::npos) {
        std::wstring key = entry.substr(0, eq);
        for (auto& [k, v] : overrides) {
          if (widen(k) == key) {
            blk += key; blk += L'='; blk += widen(v); blk += L'\0';
            replaced = true;
            break;
          }
        }
      }
      if (!replaced) { blk += entry; blk += L'\0'; }
      p += std::wcslen(p) + 1;
    }
    FreeEnvironmentStringsW(cur);
  }
  // Any override not in the parent environment is appended.
  for (auto& [k, v] : overrides) {
    std::wstring key = widen(k);
    bool found = false;
    std::wstring scan(blk);
    std::size_t pos = 0;
    while (pos < scan.size()) {
      std::size_t end = scan.find(L'\0', pos);
      std::wstring entry = scan.substr(pos, end - pos);
      if (entry.find(key + L'=') == 0) { found = true; break; }
      pos = end + 1;
    }
    if (!found) { blk += key; blk += L'='; blk += widen(v); blk += L'\0'; }
  }
  blk += L'\0';
  return blk;
}

} // namespace

Process::~Process() { close(); }

Process::Process(Process&& other) noexcept {
  process_ = other.process_; pid_ = other.pid_;
  pipe_out_ = other.pipe_out_; pipe_err_ = other.pipe_err_;
  exited_ = other.exited_; exit_code_ = other.exit_code_;
  other.process_ = nullptr; other.pipe_out_ = nullptr; other.pipe_err_ = nullptr;
}

Process& Process::operator=(Process&& other) noexcept {
  if (this != &other) {
    close();
    process_ = other.process_; pid_ = other.pid_;
    pipe_out_ = other.pipe_out_; pipe_err_ = other.pipe_err_;
    exited_ = other.exited_; exit_code_ = other.exit_code_;
    other.process_ = nullptr; other.pipe_out_ = nullptr; other.pipe_err_ = nullptr;
  }
  return *this;
}

Status Process::launch(const std::string& executable,
                       const std::vector<std::string>& arguments,
                       const std::string& working_dir,
                       const std::map<std::string, std::string>& environment,
                       Process& out) {
  out.close();

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof sa;
  sa.bInheritHandle = TRUE;

  HANDLE child_out_read = nullptr, child_out_write = nullptr;
  HANDLE child_err_read = nullptr, child_err_write = nullptr;
  if (!CreatePipe(&child_out_read, &child_out_write, &sa, 0)) {
    return Status::error(StatusCode::process_error, "CreatePipe(stdout) failed: " + last_error_message());
  }
  if (!CreatePipe(&child_err_read, &child_err_write, &sa, 0)) {
    CloseHandle(child_out_read); CloseHandle(child_out_write);
    return Status::error(StatusCode::process_error, "CreatePipe(stderr) failed: " + last_error_message());
  }
  SetHandleInformation(child_out_read, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(child_err_read, HANDLE_FLAG_INHERIT, 0);

  std::wstring cmdline = widen(build_command_line(executable, arguments));
  std::wstring wdir = widen(working_dir);
  std::wstring envblk = build_environment_block(environment);

  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = child_out_write;
  si.hStdError = child_err_write;

  PROCESS_INFORMATION pi{};
  std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
  buf.push_back(L'\0');

  BOOL ok = CreateProcessW(
      nullptr, buf.data(),
      nullptr, nullptr, TRUE,
      CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
      envblk.empty() ? nullptr : envblk.data(),
      wdir.empty() ? nullptr : wdir.c_str(),
      &si, &pi);

  // Parent's write ends are no longer needed.
  CloseHandle(child_out_write);
  CloseHandle(child_err_write);

  if (!ok) {
    CloseHandle(child_out_read);
    CloseHandle(child_err_read);
    return Status::error(StatusCode::process_error,
                         "CreateProcess failed for '" + executable + "': " + last_error_message());
  }

  out.process_ = pi.hProcess;
  out.pid_ = static_cast<std::uint64_t>(pi.dwProcessId);
  out.pipe_out_ = child_out_read;
  out.pipe_err_ = child_err_read;
  out.exited_ = false;
  out.exit_code_ = 0;
  CloseHandle(pi.hThread);
  return Status::ok();
}

bool Process::alive() {
  if (!process_) return false;
  DWORD code = 0;
  if (GetExitCodeProcess(static_cast<HANDLE>(process_), &code)) {
    // STILL_ACTIVE (259) means running.
    if (code == STILL_ACTIVE) return true;
    exited_ = true; exit_code_ = static_cast<int>(code);
    return false;
  }
  return true; // unknown; assume alive
}

Status Process::terminate(bool graceful) {
  if (!process_) return Status::error(StatusCode::process_error, "no process");
  if (graceful) {
    // Best-effort: no reliable graceful signal on Windows console apps; treat
    // graceful as request and fall through to WaitForSingleObject briefly.
    if (WaitForSingleObject(static_cast<HANDLE>(process_), 0) == WAIT_OBJECT_0) {
      exited_ = true;
      return Status::ok();
    }
    // Already dead or still running; a graceful path without a message loop
    // cannot be forced, so we report that a forced kill is required.
    return Status::error(StatusCode::permission_denied,
                         "graceful termination not supported for this target; use forced kill");
  }
  if (!TerminateProcess(static_cast<HANDLE>(process_), 1)) {
    return Status::error(StatusCode::process_error, "TerminateProcess failed: " + last_error_message());
  }
  exited_ = true;
  return Status::ok();
}

Status Process::wait(std::uint32_t timeout_ms) {
  if (!process_) return Status::error(StatusCode::process_error, "no process");
  DWORD r = WaitForSingleObject(static_cast<HANDLE>(process_), timeout_ms);
  if (r == WAIT_TIMEOUT) {
    return Status::error(StatusCode::process_error, "wait timed out (" + std::to_string(timeout_ms) + " ms)");
  }
  DWORD code = 0;
  GetExitCodeProcess(static_cast<HANDLE>(process_), &code);
  exited_ = true; exit_code_ = static_cast<int>(code);
  return Status::ok();
}

std::string Process::read_stdout() {
  std::string out;
  if (!pipe_out_) return out;
  char buf[4096];
  while (true) {
    DWORD avail = 0;
    if (!PeekNamedPipe(static_cast<HANDLE>(pipe_out_), nullptr, 0, nullptr, &avail, nullptr)) {
      if (GetLastError() == ERROR_BROKEN_PIPE) break;
      break;
    }
    if (avail == 0) break;
    DWORD read = 0;
    if (!ReadFile(static_cast<HANDLE>(pipe_out_), buf, std::min<DWORD>(avail, sizeof buf), &read, nullptr) || read == 0) break;
    out.append(buf, read);
  }
  return out;
}

std::string Process::read_stderr() {
  std::string out;
  if (!pipe_err_) return out;
  char buf[4096];
  while (true) {
    DWORD avail = 0;
    if (!PeekNamedPipe(static_cast<HANDLE>(pipe_err_), nullptr, 0, nullptr, &avail, nullptr)) break;
    if (avail == 0) break;
    DWORD read = 0;
    if (!ReadFile(static_cast<HANDLE>(pipe_err_), buf, std::min<DWORD>(avail, sizeof buf), &read, nullptr) || read == 0) break;
    out.append(buf, read);
  }
  return out;
}

void Process::close() noexcept {
  if (pipe_out_) { CloseHandle(static_cast<HANDLE>(pipe_out_)); pipe_out_ = nullptr; }
  if (pipe_err_) { CloseHandle(static_cast<HANDLE>(pipe_err_)); pipe_err_ = nullptr; }
  if (process_) { CloseHandle(static_cast<HANDLE>(process_)); process_ = nullptr; }
  pid_ = 0; exited_ = false; exit_code_ = 0;
}

} // namespace chaoslab

#else // !_WIN32

namespace chaoslab {
Process::~Process() {}
Process::Process(Process&&) noexcept {}
Process& Process::operator=(Process&&) noexcept { return *this; }
Status Process::launch(const std::string&, const std::vector<std::string>&, const std::string&,
                       const std::map<std::string, std::string>&, Process&) {
  return Status::error(StatusCode::not_supported, "process control is Windows-only");
}
bool Process::alive() { return false; }
Status Process::terminate(bool) { return Status::error(StatusCode::not_supported, "process control is Windows-only"); }
Status Process::wait(std::uint32_t) { return Status::error(StatusCode::not_supported, "process control is Windows-only"); }
std::string Process::read_stdout() { return {}; }
std::string Process::read_stderr() { return {}; }
void Process::close() noexcept {}
} // namespace chaoslab

#endif
