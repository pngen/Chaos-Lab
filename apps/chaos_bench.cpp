#include "chaoslab/digest.h"
#include "chaoslab/evidence.h"
#include "chaoslab/process.h"
#include "chaoslab/runtime_protocol.h"
#include "chaoslab/scheduler.h"
#include "chaoslab/scenario.h"
#include "chaoslab/tcp.h"
#include "chaoslab/transport.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

using namespace chaoslab;
namespace clk = std::chrono;

double ns_to_ms(clk::steady_clock::duration d) {
  return clk::duration_cast<clk::microseconds>(d).count() / 1000.0;
}

int main() {
#ifdef NDEBUG
  std::printf("Chaos Lab benchmark (Release)\n");
#else
  std::printf("Chaos Lab benchmark (Debug)\n");
#endif

  // 1. Evidence ingestion.
  {
    const int n = 100000;
    EvidenceRecorder r;
    auto t0 = clk::steady_clock::now();
    for (int i = 0; i < n; ++i) r.record(EvidenceKind::MISC, "p", {{"i", std::to_string(i)}}, "x");
    std::printf("evidence_ingest      %9.2f ms (%d records)\n", ns_to_ms(clk::steady_clock::now() - t0), n);
  }

  // 2. Evidence serialization + reload + digest.
  {
    const int n = 50000;
    EvidenceRecorder r;
    for (int i = 0; i < n; ++i) r.record(EvidenceKind::MISC, "p", {{"i", std::to_string(i)}}, "x");
    auto t0 = clk::steady_clock::now();
    std::string text = r.serialize_text();
    auto t1 = clk::steady_clock::now();
    std::vector<EvidenceRecord> out;
    bool ok = EvidenceRecorder::parse_text(text, out);
    auto t2 = clk::steady_clock::now();
    Digest256 d = r.digest();
    auto t3 = clk::steady_clock::now();
    (void)d; (void)ok;
    std::printf("evidence_serialize   %9.2f ms (%zu bytes)\n", ns_to_ms(t1 - t0), text.size());
    std::printf("evidence_reload      %9.2f ms\n", ns_to_ms(t2 - t1));
    std::printf("evidence_digest      %9.2f ms\n", ns_to_ms(t3 - t2));
  }

  // 3. Digest throughput.
  {
    const int n = 200000;
    auto t0 = clk::steady_clock::now();
    Digest256 acc;
    for (int i = 0; i < n; ++i) acc = Digest256::sha256(std::to_string(i));
    std::printf("digest_sha256        %9.2f ms (%d hashes)\n", ns_to_ms(clk::steady_clock::now() - t0), n);
    (void)acc;
  }

  // 4. Fault scheduling / plan determinism.
  {
    const int n = 2000;
    ScenarioBuilder b(0xbeef);
    b.target_worker("w", "target_worker");
    for (int i = 0; i < n; ++i) b.inject_fault(static_cast<FaultCategory>(i % 40), TargetId((1ull << 63) | 1));
    Scenario s = b.build();
    auto t0 = clk::steady_clock::now();
    FaultScheduler sc(s.campaign.seed);
    sc.plan(s.campaign);
    for (int i = 0; i < n; ++i) sc.observe_event("completion_boundary");
    std::printf("fault_schedule       %9.2f ms (%d faults)\n", ns_to_ms(clk::steady_clock::now() - t0), n);
  }

  // 5. Frame codec throughput.
  {
    const int n = 200000;
    Frame f; f.type = 0x42; f.payload.assign(128, 0x7f);
    auto bytes = f.encode();
    auto t0 = clk::steady_clock::now();
    for (int i = 0; i < n; ++i) { Frame out; std::size_t c = 0; Frame::decode(bytes.data(), bytes.size(), out, c); }
    std::printf("frame_codec          %9.2f ms (%d frames)\n", ns_to_ms(clk::steady_clock::now() - t0), n);
  }

  // 6. Proxy throughput (no faults) over loopback TCP with an echo server.
  if (tcp_init().okay()) {
    const int n = 2000;
    int srv = -1; std::uint16_t sp = 0;
    if (tcp_listen(0, srv, sp).okay()) {
      std::atomic<bool> stop{false};
      std::thread server([&] {
        int c = -1; TcpEndpoint p;
        if (tcp_accept(srv, c, p).failed()) return;
        while (!stop.load()) { std::uint32_t t = 0; std::string pay; if (proto::recv_frame(c, t, pay).failed()) break; proto::send_frame(c, t, pay); }
        tcp_close(c);
      });
      FaultyTransport proxy;
      if (proxy.start(0, "127.0.0.1", sp).okay()) {
        int cfd = -1;
        if (tcp_connect("127.0.0.1", proxy.listen_port(), cfd).okay()) {
          auto t0 = clk::steady_clock::now();
          for (int i = 0; i < n; ++i) proto::send_frame(cfd, 0x100 + static_cast<std::uint32_t>(i), "bench");
          // wait for the echoed rounds to drain
          std::this_thread::sleep_for(std::chrono::milliseconds(300));
          auto t1 = clk::steady_clock::now();
          std::printf("proxy_throughput     %9.2f ms (%d frames, no fault)\n", ns_to_ms(t1 - t0), n);
          std::printf("proxy_frames_fwd     %9llu\n", (unsigned long long)proxy.stats().frames_forwarded);
          tcp_close(cfd);
        }
        proxy.stop();
      }
      stop.store(true);
      if (server.joinable()) server.join();
    }
    tcp_close(srv);
    tcp_shutdown();
  }

  // 7. Process launch/cleanup.
  {
    const int n = 20;
    auto t0 = clk::steady_clock::now();
    for (int i = 0; i < n; ++i) {
      Process p;
      if (Process::launch("cmd.exe", {"/c", "exit", "0"}, "", {}, p).okay()) { p.wait(5000); p.close(); }
    }
    std::printf("process_lifecycle    %9.2f ms (%d processes)\n", ns_to_ms(clk::steady_clock::now() - t0), n);
  }

  std::printf("bench_done\n");
  return 0;
}
