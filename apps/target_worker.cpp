#include "chaoslab/runtime_protocol.h"
#include "chaoslab/tcp.h"
#include "chaoslab/text.h"
#include "chaoslab/digest.h"
#ifdef CHAOSLAB_HAS_CUDA
#include "chaoslab/cuda.h"
#endif

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace chaoslab;

namespace {

std::string g_name = "worker";
std::uint64_t g_id = 1;
std::uint64_t g_boot = 1;
bool g_cuda = false;
bool g_smoke = false;

std::string do_work(const std::string& work) {
  // Deterministic result: hashes the work payload plus the worker's boot id so
  // a different boot produces a different result.
  Sha256 h;
  h.update(work);
  h.update("|boot=" + std::to_string(g_boot));
  return h.finish().hex();
}

#ifdef CHAOSLAB_HAS_CUDA
bool cuda_roundtrip() {
  DeviceInfo info;
  if (cuda_query_device(info).failed() || info.name.empty()) {
    printf("CUDA|no_device\n"); fflush(stdout);
    return false;
  }
  const std::size_t n = 1024 * 1024; // 8 MiB
  std::vector<double> x(n, 1.0), y(n, 0.0), yref(n, 0.0);
  DeviceBuffer bx, by;
  if (bx.allocate(n * sizeof(double), 0).failed()) { printf("CUDA|alloc_failed\n"); fflush(stdout); return false; }
  if (by.allocate(n * sizeof(double), 0).failed()) { printf("CUDA|alloc_failed\n"); fflush(stdout); return false; }
  if (cuda_h2d(bx.data(), x.data(), n * sizeof(double)).failed()) return false;
  if (cuda_h2d(by.data(), y.data(), n * sizeof(double)).failed()) return false;
  if (cuda_saxpy(n, 2.0, static_cast<double*>(bx.data()), static_cast<double*>(by.data())).failed()) return false;
  if (cuda_d2h(y.data(), by.data(), n * sizeof(double)).failed()) return false;
  bool ok = true;
  for (std::size_t i = 0; i < n; ++i) if (y[i] != 2.0) { ok = false; break; }
  printf("CUDA|result=%s\n", ok ? "ok" : "mismatch"); fflush(stdout);
  return ok;
}
#endif

void handle_dispatch(const std::string& payload, int fd) {
  auto parts = split(payload, '|');
  if (parts.size() != 5) return;
  std::string epoch = parts[0];
  std::string attempt = parts[1];
  std::string gen = parts[2];
  std::string dispatch = parts[3];
  std::string work = parts[4];
  proto::send_frame(fd, proto::MsgType::WORK_STARTED, "started");
#ifdef CHAOSLAB_HAS_CUDA
  if (g_cuda && cuda_roundtrip()) {
    // The result incorporates the GPU verification.
    work += ":gpu";
  }
#endif
  std::string result = do_work(work);
  std::string complete = epoch + "|" + attempt + "|" + gen + "|" + dispatch + "|" +
                         std::to_string(g_id) + "|" + std::to_string(g_boot) + "|" + result;
  proto::send_frame(fd, proto::MsgType::COMPLETE, complete);
}

} // namespace

int main(int argc, char** argv) {
  std::string coord_host = "127.0.0.1";
  std::uint16_t coord_port = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--coord" && i + 1 < argc) {
      std::string c = argv[++i];
      auto p = c.rfind(':');
      if (p != std::string::npos) { coord_host = c.substr(0, p); coord_port = static_cast<std::uint16_t>(std::atoi(c.substr(p + 1).c_str())); }
    } else if (a == "--name" && i + 1 < argc) g_name = argv[++i];
    else if (a == "--id" && i + 1 < argc) g_id = std::stoull(argv[++i]);
    else if (a == "--boot" && i + 1 < argc) g_boot = std::stoull(argv[++i]);
    else if (a == "--cuda") g_cuda = true;
    else if (a == "--smoke") g_smoke = true;
  }
  if (g_smoke) {
    if (tcp_init().failed()) { printf("ERR|tcp_init\n"); return 2; }
#ifdef CHAOSLAB_HAS_CUDA
    bool ok = cuda_roundtrip();
    printf("SMOKE|result=%d\n", ok ? 1 : 0); fflush(stdout);
    tcp_shutdown();
    return ok ? 0 : 1;
#else
    printf("SMOKE|no_cuda\n"); fflush(stdout);
    tcp_shutdown();
    return 1;
#endif
  }
  if (coord_port == 0) { printf("ERR|no_coord\n"); return 2; }
  if (tcp_init().failed()) { printf("ERR|tcp_init\n"); return 2; }
  // Reconnect/re-register on coordinator loss so the worker survives coordinator
  // restarts and failover. Bounded attempt count; no test timeout semantics.
  int attempts = 0;
  while (attempts < 200) {
    int fd = -1;
    if (tcp_connect(coord_host, coord_port, fd).failed()) {
      ++attempts;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    printf("CONNECT|id=%llu;boot=%llu\n", (unsigned long long)g_id, (unsigned long long)g_boot); fflush(stdout);
    proto::send_frame(fd, proto::MsgType::REGISTER, g_name + "|" + std::to_string(g_id) + "|" + std::to_string(g_boot));

    bool lost = false;
    while (true) {
      std::uint32_t type = 0; std::string payload;
      Status s = proto::recv_frame(fd, type, payload);
      if (s.failed()) { lost = true; break; }
      switch (type) {
        case proto::MsgType::DISPATCH: handle_dispatch(payload, fd); break;
        case proto::MsgType::COMPLETE_ACK: printf("ACK|%s\n", payload.c_str()); fflush(stdout); break;
        case proto::MsgType::COMPLETE_REJECTED: printf("REJECTED|%s\n", payload.c_str()); fflush(stdout); break;
        case proto::MsgType::REGISTERED: printf("REGISTERED|epoch=%s\n", payload.c_str()); fflush(stdout); break;
        case proto::MsgType::PING: proto::send_frame(fd, proto::MsgType::PONG, "pong"); break;
        case proto::MsgType::SHUTDOWN: tcp_close(fd); tcp_shutdown(); return 0;
        default: break;
      }
    }
    tcp_close(fd);
    if (lost) {
      printf("LOST|id=%llu;boot=%llu\n", (unsigned long long)g_id, (unsigned long long)g_boot); fflush(stdout);
      ++attempts;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } else {
      break; // clean shutdown path
    }
  }
  tcp_shutdown();
  return 0;
}
