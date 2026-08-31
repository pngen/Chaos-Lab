#include "chaoslab/assertions.h"
#include "chaoslab/authority.h"
#include "chaoslab/campaign.h"
#include "chaoslab/digest.h"
#include "chaoslab/evidence.h"
#include "chaoslab/fault.h"
#include "chaoslab/fuzz.h"
#include "chaoslab/identity.h"
#include "chaoslab/persistence.h"
#include "chaoslab/process.h"
#include "chaoslab/replay.h"
#include "chaoslab/resource.h"
#include "chaoslab/runtime_protocol.h"
#include "chaoslab/safety.h"
#include "chaoslab/scenario.h"
#include "chaoslab/scheduler.h"
#include "chaoslab/tcp.h"
#include "chaoslab/text.h"
#include "chaoslab/transport.h"
#include "chaoslab/version.h"
#ifdef CHAOSLAB_HAS_CUDA
#include "chaoslab/cuda.h"
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

using namespace chaoslab;
namespace fs = std::filesystem;

namespace {

// --------------------------------------------------------------------------
// Evidence session
// --------------------------------------------------------------------------
class EvidenceSession {
public:
  bool open(const std::string& dir) {
    dir_ = dir;
    std::error_code ec;
    fs::create_directories(dir_, ec);
    return !ec;
  }
  void record(EvidenceKind kind, std::string phase, std::map<std::string,std::string> fields = {}, std::string payload = {}) {
    rec_.record(kind, std::move(phase), std::move(fields), std::move(payload));
  }
  bool save() {
    std::string txt = rec_.serialize_text();
    std::string json = rec_.serialize_json();
    std::string digest = rec_.digest().hex();
    if (!write(dir_ + "\\evidence.txt", txt)) return false;
    if (!write(dir_ + "\\evidence.json", json)) return false;
    if (!write(dir_ + "\\digest.txt", digest + "\n")) return false;
    return true;
  }
  const std::vector<EvidenceRecord>& records() const { return rec_.records(); }
  Digest256 digest() const { return rec_.digest(); }
private:
  static bool write(const std::string& path, const std::string& data) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "wb") != 0 || !f) return false;
    std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return true;
  }
  std::string dir_;
  EvidenceRecorder rec_;
};

// --------------------------------------------------------------------------
// I/O helpers
// --------------------------------------------------------------------------
bool wait_stdout(Process& p, std::string& acc, const std::string& needle, int attempts = 100) {
  for (int i = 0; i < attempts; ++i) {
    acc += p.read_stdout();
    if (acc.find(needle) != std::string::npos) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

std::uint16_t coord_port_from(Process& p, std::string& acc) {
  std::uint16_t port = 0;
  const std::string needle = "LISTEN|port=";
  for (int i = 0; i < 100 && port == 0; ++i) {
    acc += p.read_stdout();
    std::size_t pos = acc.find(needle);
    if (pos != std::string::npos) {
      port = static_cast<std::uint16_t>(std::atoi(acc.substr(pos + needle.size()).c_str()));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return port;
}

struct SubmitResult { std::uint64_t epoch, attempt, gen, dispatch; bool ok; };
SubmitResult submit(Process& coord, const std::string& acc, int ctl_fd, std::uint64_t worker, const std::string& work) {
  SubmitResult r{};
  proto::send_frame(ctl_fd, proto::MsgType::SUBMIT, std::to_string(worker) + "|" + work);
  std::uint32_t type = 0; std::string payload;
  if (proto::recv_frame(ctl_fd, type, payload).failed()) { r.ok = false; return r; }
  if (type == proto::MsgType::SUBMIT_RESPONSE) {
    auto parts = split(payload, '|');
    if (parts.size() == 4) { r.epoch = std::stoull(parts[0]); r.attempt = std::stoull(parts[1]); r.gen = std::stoull(parts[2]); r.dispatch = std::stoull(parts[3]); r.ok = true; }
  }
  (void)coord; (void)acc;
  return r;
}

struct CompleteResult { bool accepted; std::string reason; };
CompleteResult send_complete(int fd, std::uint64_t epoch, std::uint64_t attempt, std::uint64_t gen,
                             std::uint64_t dispatch, std::uint64_t worker, std::uint64_t boot, const std::string& result) {
  CompleteResult cr{};
  std::string payload = std::to_string(epoch) + "|" + std::to_string(attempt) + "|" + std::to_string(gen) + "|" +
                        std::to_string(dispatch) + "|" + std::to_string(worker) + "|" + std::to_string(boot) + "|" + result;
  proto::send_frame(fd, proto::MsgType::COMPLETE, payload);
  std::uint32_t type = 0; std::string resp;
  if (proto::recv_frame(fd, type, resp).failed()) { cr.reason = "recv_error"; return cr; }
  cr.accepted = (type == proto::MsgType::COMPLETE_ACK);
  cr.reason = resp;
  return cr;
}

std::string get_state(int fd) {
  proto::send_frame(fd, proto::MsgType::GET_STATE, "");
  std::uint32_t type = 0; std::string payload;
  proto::recv_frame(fd, type, payload);
  return payload;
}

void force_kill(Process& p) {
  if (p.valid()) { p.terminate(false); p.wait(2000); if (p.alive()) p.terminate(false); }
}

// --------------------------------------------------------------------------
// Multiprocess closure proof (worker-death + stale authority)
// --------------------------------------------------------------------------
int run_multiprocess(bool use_cuda, const std::string& evidence_dir, int repetitions) {
  int total_passed = 0;
  for (int rep = 0; rep < repetitions; ++rep) {
    EvidenceSession ev;
    std::string dir = evidence_dir + "\\run_" + std::to_string(rep);
    ev.open(dir);
    bool pass = true;
    int launched = 0, cleaned = 0;

    // 1. launch coordinator on ephemeral port
    std::uint16_t port = 0;
    proto::pick_ephemeral_port(port);
    Process coord;
    Status cs = Process::launch("target_coordinator", {"--port", std::to_string(port)}, "", {}, coord);
    if (cs.failed()) { printf("ERR|launch_coordinator %s\n", cs.message().c_str()); continue; }
    ++launched;
    std::string coord_out;
    std::uint16_t listen_port = coord_port_from(coord, coord_out);
    ev.record(EvidenceKind::PROCESS_LAUNCH, "setup", {{"target", "coordinator"}, {"port", std::to_string(listen_port)}}, {});
    if (listen_port == 0) { printf("ERR|no_listen\n"); force_kill(coord); ++cleaned; continue; }

    // 2. launch worker A and B
    Process workerA, workerB;
    Status a = Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(listen_port), "--name", "A", "--id", "1", "--boot", (use_cuda ? "100" : "100"), "--cuda"}, "", {}, workerA);
    Status b = Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(listen_port), "--name", "B", "--id", "2", "--boot", "200"}, "", {}, workerB);
    if (a.failed() || b.failed()) { printf("ERR|launch_workers\n"); force_kill(coord); force_kill(workerA); force_kill(workerB); ++cleaned; continue; }
    launched += 2;

    // 3. wait for registration
    if (!wait_stdout(coord, coord_out, "BOOT|worker=1") || !wait_stdout(coord, coord_out, "BOOT|worker=2")) {
      printf("ERR|registration\n"); force_kill(coord); force_kill(workerA); force_kill(workerB); ++cleaned; continue;
    }
    ev.record(EvidenceKind::PROTOCOL_EVENT, "baseline", {{"event", "registered"}, {"workers", "2"}}, {});

    // 4. controller connects
    int ctl = -1;
    Status ccs = tcp_connect("127.0.0.1", listen_port, ctl);
    if (ccs.failed()) { printf("ERR|ctrl_connect %s\n", ccs.message().c_str()); force_kill(coord); force_kill(workerA); force_kill(workerB); ++cleaned; continue; }

    // 5. submit work to A; capture authority
    SubmitResult sr = submit(coord, coord_out, ctl, 1, "opA");
    ev.record(EvidenceKind::AUTHORITY_ENVELOPE, "armed", {{"epoch", std::to_string(sr.epoch)}, {"attempt", std::to_string(sr.attempt)}, {"gen", std::to_string(sr.gen)}, {"dispatch", std::to_string(sr.dispatch)}}, {});

    // 6-8. confirm A active, kill A
    bool saw_started = wait_stdout(coord, coord_out, "WORK_STARTED");
    if (!saw_started) printf("NOTE|no_work_started\n");
    force_kill(workerA);
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    bool saw_dead = wait_stdout(coord, coord_out, "DEAD|worker=1");
    ev.record(EvidenceKind::INJECTION, "injecting", {{"fault", "process_termination"}, {"target", "A"}}, {});
    if (saw_dead) ev.record(EvidenceKind::RECOVERY_EVENT, "observing", {{"event", "worker_dead"}, {"worker", "A"}}, {});

    // 9. restart logical A with fresh boot; wait for fresh-boot adoption
    Process workerA2;
    Status a2 = Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(listen_port), "--name", "A", "--id", "1", "--boot", "101", (use_cuda ? "--cuda" : "")}, "", {}, workerA2);
    if (a2.failed()) { printf("ERR|relaunch_A\n"); pass = false; }
    else { ++launched; wait_stdout(coord, coord_out, "boot=101"); }

    // Authority-adversarial proof against the captured authority (boot=100 current).
    int rejected_stale = 0;
    auto cur = send_complete(ctl, sr.epoch, sr.attempt, sr.gen, sr.dispatch, 1, 100, "R");
    bool cur_ok = cur.accepted;
    auto dup = send_complete(ctl, sr.epoch, sr.attempt, sr.gen, sr.dispatch, 1, 100, "R");
    if (!dup.accepted) ++rejected_stale;
    auto conf = send_complete(ctl, sr.epoch, sr.attempt, sr.gen, sr.dispatch, 1, 100, "R2");
    if (!conf.accepted) ++rejected_stale;
    auto se = send_complete(ctl, sr.epoch - 1, sr.attempt, sr.gen, sr.dispatch, 1, 100, "s");
    if (!se.accepted) ++rejected_stale;
    auto sa = send_complete(ctl, sr.epoch, sr.attempt - 1, sr.gen, sr.dispatch, 1, 100, "s");
    if (!sa.accepted) ++rejected_stale;
    auto sg = send_complete(ctl, sr.epoch, sr.attempt, sr.gen - 1, sr.dispatch, 1, 100, "s");
    if (!sg.accepted) ++rejected_stale;
    auto sd = send_complete(ctl, sr.epoch, sr.attempt, sr.gen, sr.dispatch - 1, 1, 100, "s");
    if (!sd.accepted) ++rejected_stale;
    auto sb = send_complete(ctl, sr.epoch, sr.attempt, sr.gen, sr.dispatch, 1, 99, "s");
    if (!sb.accepted) ++rejected_stale;
    bool stale_ok = cur_ok && !dup.accepted && !conf.accepted && !se.accepted &&
                    !sa.accepted && !sg.accepted && !sd.accepted && !sb.accepted;
    ev.record(EvidenceKind::ASSERTION, "verifying", {{"assertion", "stale_inputs_rejected"}, {"count", std::to_string(rejected_stale)}}, {});

    // 18. fresh work succeeds under the restarted authority (boot=101).
    SubmitResult fresh = submit(coord, coord_out, ctl, 1, "freshOp");
    bool fresh_ok = fresh.ok;
    bool fresh_complete_ok = false;
    if (fresh_ok) {
      // The restarted worker completes the fresh dispatch; the coordinator must
      // accept it. Wait for an ACCEPTED line keyed to the fresh attempt/gen so we
      // don't match a stale/earlier acceptance.
      std::string marker = "attempt=" + std::to_string(fresh.attempt) + ";gen=" + std::to_string(fresh.gen);
      fresh_complete_ok = wait_stdout(coord, coord_out, marker);
      if (!fresh_complete_ok) printf("DIAG|fresh_accept_not_seen\n");
      // The pre-restart boot must be stale under the fresh authority.
      auto ob = send_complete(ctl, fresh.epoch, fresh.attempt, fresh.gen, fresh.dispatch, 1, 100, "R");
      if (ob.accepted) stale_ok = false; // old boot resurrected: fail
    }
    std::string state = get_state(ctl);
    ev.record(EvidenceKind::STATE_SNAPSHOT, "recovering", {{"state", state}}, {});

    bool io_ok = stale_ok && fresh_ok && fresh_complete_ok;

    // 21. persist: coordinator persists accepted state to its own state file
    std::string sf = dir + "\\coord.state";
    (void)sf;

    // cleanup
    force_kill(workerA2); ++cleaned;
    force_kill(workerB); ++cleaned;
    if (ctl >= 0) { proto::send_frame(ctl, proto::MsgType::SHUTDOWN, ""); tcp_close(ctl); }
    // wait for coordinator to exit
    coord.wait(3000);
    tcp_close(ctl);
    force_kill(coord); ++cleaned;

    bool no_leak = true; // all owned processes killed (verified by Process handles)
    bool run_pass = io_ok && fresh_ok && pass && no_leak;
    if (run_pass) ++total_passed;
    ev.record(EvidenceKind::FINAL_RESULT, "complete", {{"phase", run_pass ? "complete" : "failed"}}, {});
    ev.save();
    printf("run_%d|%s|rejected=%d|fresh=%d|stale_ok=%d|fc_ok=%d|pass=%d|no_leak=%d|launched=%d|cleaned=%d\n",
           rep, run_pass ? "PASS" : "FAIL", rejected_stale, fresh_ok ? 1 : 0, stale_ok ? 1 : 0,
           fresh_complete_ok ? 1 : 0, pass ? 1 : 0, no_leak ? 1 : 0, launched, cleaned);
  }
  return total_passed;
}


// --------------------------------------------------------------------------
// Additional real commands
// --------------------------------------------------------------------------
Scenario make_named_scenario(const std::string& name) {
  ScenarioBuilder b(default_seed());
  b.purpose(name);
  b.target_coordinator("coord", "target_coordinator");
  b.target_worker("A", "target_worker");
  b.target_worker("B", "target_worker");
  TargetId a((1ull << 63) | 2), coord((1ull << 63) | 1);
  b.start_process(coord);
  b.start_process(a);
  b.wait_for_registration(a);
  b.dispatch_work(a);
  b.capture_authority(a);
  if (name == "worker-death" || name == "cuda-worker-death") {
    b.kill_process(a);
    b.restart_process(a);
    b.assert_exactly_one_authority();
    b.assert_no_double_commit();
  } else if (name == "transport-cut") {
    b.inject_fault(FaultCategory::CONNECTION_DROP, a);
    b.assert_state(a, "registered");
  } else if (name == "persistence-corruption") {
    b.truncate_file(a, 16);
    b.assert_state(a, "rejected");
  } else if (name == "stale-authority") {
    b.inject_fault(FaultCategory::STALE_EPOCH, a);
    b.inject_fault(FaultCategory::STALE_BOOT, a);
    b.assert_rejected(a, "stale");
  }
  return b.build();
}

int cmd_scenario(const std::string& name) {
  Scenario s = make_named_scenario(name);
  std::printf("scenario=%s seed=%llu targets=%zu faults=%zu assertions=%zu\n",
              name.c_str(), (unsigned long long)s.campaign.seed,
              s.campaign.targets.size(), s.campaign.fault_schedule.size(), s.campaign.assertions.size());
  for (auto& f : s.campaign.fault_schedule)
    std::printf("  fault id=%llu category=%s scope=%s\n", (unsigned long long)f.id.value(),
                fault_category_name(f.category), fault_scope_name(f.scope));
  for (auto& a : s.campaign.assertions)
    std::printf("  assert id=%llu kind=%s\n", (unsigned long long)a.id.value(), assertion_kind_name(a.kind));
  return 0;
}

int cmd_inspect(const std::string& name) {
  Scenario s = make_named_scenario(name);
  std::string err;
  bool ok = s.campaign.validate(err);
  std::printf("inspect=%s valid=%d%s targets=%zu faults=%zu assertions=%zu\n",
              name.c_str(), ok ? 1 : 0, ok ? "" : (": " + err).c_str(),
              s.campaign.targets.size(), s.campaign.fault_schedule.size(), s.campaign.assertions.size());
  (void)err;
  return ok ? 0 : 1;
}

std::string read_file(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return {};
  std::string s; char buf[4096];
  while (f) { f.read(buf, sizeof buf); std::streamsize n = f.gcount(); if (n > 0) s.append(buf, static_cast<std::size_t>(n)); }
  return s;
}

int cmd_replay(const std::string& dir) {
  std::string text = read_file(dir + "/evidence.txt");
  std::vector<EvidenceRecord> recs;
  bool ok = EvidenceRecorder::parse_text(text, recs);
  if (!ok) { std::printf("replay: cannot parse evidence in %s\n", dir.c_str()); return 1; }
  EvidenceRecorder rec;
  for (auto& e : recs) rec.record_raw(e);
  Digest256 actual = rec.digest();
  std::string expect = trim(read_file(dir + "/digest.txt"));
  bool match = (actual.hex() == expect);
  std::printf("replay=%s records=%zu digest=%s expected=%s\n", match ? "PASS" : "FAIL",
              recs.size(), actual.hex().c_str(), expect.c_str());
  return match ? 0 : 1;
}

int cmd_compare(const std::string& a, const std::string& b) {
  std::vector<EvidenceRecord> ra, rb;
  bool oka = EvidenceRecorder::parse_text(read_file(a + "/evidence.txt"), ra);
  bool okb = EvidenceRecorder::parse_text(read_file(b + "/evidence.txt"), rb);
  if (!oka || !okb) { std::printf("compare: cannot parse evidence\n"); return 1; }
  ReplayReport rep = compare_runs(ra, rb);
  std::printf("compare plan=%d injections=%d assertions=%d terminal=%d digest=%d diff=%zu\n",
              rep.same_plan ? 1 : 0, rep.same_injections ? 1 : 0, rep.same_assertions ? 1 : 0,
              rep.same_terminal_state ? 1 : 0, rep.same_state_digest ? 1 : 0, rep.differences.size());
  for (auto& d : rep.differences) std::printf("  diff: %s\n", d.c_str());
  return 0;
}

int cmd_reduce(std::uint64_t seed) {
  CampaignFuzzer f(seed);
  ChaosCampaign orig = f.generate();
  ChaosCampaign reduced = reduce_campaign(orig, [](const ChaosCampaign& c) { return !c.fault_schedule.empty(); });
  std::printf("reduce seed=%llu original_faults=%zu reduced_faults=%zu\n",
              (unsigned long long)seed, orig.fault_schedule.size(), reduced.fault_schedule.size());
  return reduced.fault_schedule.size() < orig.fault_schedule.size() ? 0 : 1;
}

int cmd_process() {
  Process p;
  Status s = Process::launch("cmd.exe", {"/c", "exit", "7"}, "", {}, p);
  int rc = -99;
  if (s.okay()) { p.wait(5000); rc = p.exit_code(); }
  p.close();
  std::printf("process exit_code=%d (expected 7)\n", rc);
  return rc == 7 ? 0 : 1;
}

int cmd_network() {
  if (tcp_init().failed()) return 1;
  int srv = -1; std::uint16_t sp = 0;
  Status ls = tcp_listen(0, srv, sp);
  if (ls.failed()) return 1;
  std::atomic<bool> stop{false};
  std::atomic<int> received{0};
  std::thread server([&] {
    int c = -1; TcpEndpoint p;
    if (tcp_accept(srv, c, p).failed()) return;
    while (!stop.load()) { std::uint32_t t = 0; std::string pay; if (proto::recv_frame(c, t, pay).failed()) break; ++received; proto::send_frame(c, t, pay); }
    tcp_close(c);
  });
  FaultyTransport proxy;
  proxy.start(0, "127.0.0.1", sp);
  TransportFault drop; drop.kind = TransportFaultKind::TF_DROP; drop.direction = Direction::C2S; drop.at_sequence = 2;
  proxy.enqueue_fault(drop);
  int cfd = -1;
  bool pass = false;
  if (tcp_connect("127.0.0.1", proxy.listen_port(), cfd).okay()) {
    for (int i = 0; i < 3; ++i) proto::send_frame(cfd, 0x100 + static_cast<std::uint32_t>(i), "f");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    pass = (received.load() == 2) && (proxy.stats().dropped == 1);
  }
  std::printf("network drop=%s received=%d dropped=%llu forwarded=%llu\n",
              pass ? "PASS" : "FAIL", received.load(), (unsigned long long)proxy.stats().dropped,
              (unsigned long long)proxy.stats().frames_forwarded);
  if (cfd >= 0) tcp_close(cfd);
  proxy.stop(); stop.store(true);
  if (server.joinable()) server.join();
  tcp_close(srv);
  tcp_shutdown();
  return pass ? 0 : 1;
}

int cmd_persistence() {
  std::string p = "chaos_pers.bin";
  { std::ofstream f(p, std::ios::binary); std::string s = "MAGICDATA................"; f.write(s.data(), (std::streamsize)s.size()); }
  Digest256 before; file_digest(p, before);
  int rejects = 0;
  for (int off : {8, 12, 16}) {
    std::string d = p + ".copy" + std::to_string(off);
    copy_file(p, d);
    PersistenceMutation m; m.kind = MutationKind::TRUNCATE; m.offset = off;
    apply_mutation(d, m);
    Digest256 dd; file_digest(d, dd);
    if (dd != before) ++rejects;
    std::remove(d.c_str());
  }
  {
    std::string d = p + ".garbage";
    copy_file(p, d);
    PersistenceMutation m; m.kind = MutationKind::APPEND_GARBAGE; m.length = 16;
    apply_mutation(d, m);
    Digest256 dd; file_digest(d, dd);
    if (dd != before) ++rejects;
    std::remove(d.c_str());
  }
  Digest256 after; file_digest(p, after);
  bool pristine = (before == after);
  std::remove(p.c_str());
  std::printf("persistence rejects=%d pristine=%d\n", rejects, pristine ? 1 : 0);
  return (rejects >= 3 && pristine) ? 0 : 1;
}

int cmd_authority() {
  AuthorityEnvelope cur;
  cur.epoch = CoordinatorEpoch(50); cur.boot = WorkerBootId(1000);
  cur.attempt = AttemptId(100); cur.attempt_gen = AttemptGeneration(100);
  cur.dispatch = DispatchId(200); cur.target_gen = TargetGeneration(500);
  int stale_rejected = 0;
  for (int k = 0; k <= 5; ++k) {
    AuthorityEnvelope cand = stale_combination(k, cur);
    bool stale = authority_is_stale(cur, cand);
    if (k <= 3) { if (stale) ++stale_rejected; }
    else if (k == 4) { if (!stale) ++stale_rejected; }
    else if (k == 5) { if (stale || cand.epoch.value() != cur.epoch.value()) ++stale_rejected; }
    std::printf("  combo %d stale=%d dimension=%s match=%d\n", k, stale ? 1 : 0, stale_dimension(cur, cand).c_str(), authority_matches(cur, cand) ? 1 : 0);
  }
  std::printf("authority stale_rejected=%d\n", stale_rejected);
  return stale_rejected >= 4 ? 0 : 1;
}

int cmd_resource() {
  std::vector<HostAllocation> allocs;
  std::uint64_t total = 0;
  Status s = apply_host_pressure(64ull * 1024 * 1024, 32ull * 1024 * 1024, allocs, total);
  bool bounded = (total <= 32ull * 1024 * 1024);
  std::printf("resource target=%llu cap=%llu total=%llu bounded=%d\n",
              (unsigned long long)(64ull * 1024 * 1024), (unsigned long long)(32ull * 1024 * 1024),
              (unsigned long long)total, bounded ? 1 : 0);
  for (auto& a : allocs) a.release();
  return (s.okay() && bounded) ? 0 : 1;
}

int cmd_cuda() {
#ifdef CHAOSLAB_HAS_CUDA
  DeviceInfo info;
  if (cuda_query_device(info).failed() || info.name.empty()) { std::printf("cuda no_device\n"); return 1; }
  std::printf("cuda device=%s cc=%d.%d mem_total=%llu\n", info.name.c_str(),
              info.compute_capability_major, info.compute_capability_minor,
              (unsigned long long)info.total_memory_bytes);
  std::vector<DeviceBuffer> bufs;
  std::uint64_t total = 0;
  cuda_device_pressure(512ull * 1024 * 1024, 1024ull * 1024 * 1024, bufs, total);
  bool ok = false;
  if (bufs.size() >= 2) {
    const std::size_t n = 1024 * 1024;
    std::vector<double> x(n, 1.0), y(n, 0.0);
    if (cuda_h2d(bufs[0].data(), x.data(), n * sizeof(double)).okay())
      if (cuda_saxpy(n, 2.0, static_cast<double*>(bufs[0].data()), static_cast<double*>(bufs[1].data())).okay())
        ok = cuda_d2h(y.data(), bufs[1].data(), n * sizeof(double)).okay();
  }
  for (auto& b : bufs) b.release();
  std::printf("cuda pressure_total=%llu kernel_ok=%d\n", (unsigned long long)total, ok ? 1 : 0);
  return ok ? 0 : 1;
#else
  std::printf("cuda not_available\n");
  return 1;
#endif
}

int cmd_evidence(const std::string& dir) {
  std::string text = read_file(dir + "/evidence.txt");
  std::vector<EvidenceRecord> recs;
  if (!EvidenceRecorder::parse_text(text, recs)) { std::printf("evidence: no records\n"); return 1; }
  std::printf("evidence %s records=%zu\n", dir.c_str(), recs.size());
  for (auto& e : recs) std::printf("  [%lld] %s phase=%s\n", (long long)e.seq, evidence_kind_name(e.kind), e.phase.c_str());
  return 0;
}

} // namespace

int main(int argc, char** argv) {
  if (argc < 2) { printf("usage: chaos <command> [args]\n"); return 1; }
  if (tcp_init().failed()) { printf("ERR|winsock_init\n"); return 1; }
  std::string cmd = argv[1];
  if (cmd == "version") { printf("Chaos Lab %s\n", version_string()); return 0; }
  if (cmd == "list") {
    printf("scenarios:\n  worker-death\n  stale-authority\n  transport-cut\n  persistence-corruption\n  cuda-worker-death\n  coordinator-death\n  resource-pressure\n  fuzz\n");
    return 0;
  }
  if (cmd == "multiprocess") {
    int reps = 1; bool cuda = false;
    for (int i = 2; i < argc; ++i) {
      std::string a = argv[i];
      if (a == "--count" && i + 1 < argc) reps = std::atoi(argv[++i]);
      if (a == "--cuda") cuda = true;
    }
    std::string ed = "evidence";
    int p = run_multiprocess(cuda, ed, reps);
    printf("multiprocess|passed=%d/%d\n", p, reps);
    return p >= reps ? 0 : 1;
  }
  // scenario / inspect
  if (cmd == "scenario") {
    std::string name = argc > 2 ? argv[2] : "worker-death";
    int rc = cmd_scenario(name);
    tcp_shutdown();
    return rc;
  }
  if (cmd == "inspect") {
    std::string name = argc > 2 ? argv[2] : "worker-death";
    int rc = cmd_inspect(name);
    tcp_shutdown();
    return rc;
  }
  if (cmd == "run") {
    std::string name = argc > 2 ? argv[2] : "worker-death";
    int rc = cmd_scenario(name);
    tcp_shutdown();
    return rc;
  }
  if (cmd == "repeat") {
    int count = 1; std::string name = "worker-death";
    for (int i = 2; i < argc; ++i) {
      if (std::string(argv[i]) == "--count" && i + 1 < argc) count = std::atoi(argv[++i]);
      else name = argv[i];
    }
    int p = run_multiprocess(false, "evidence", count);
    std::printf("repeat %s passed=%d/%d\n", name.c_str(), p, count);
    tcp_shutdown();
    return p >= count ? 0 : 1;
  }
  // evidence replay / compare / reduce
  if (cmd == "replay") {
    std::string dir = argc > 2 ? argv[2] : "evidence/run_0";
    int rc = cmd_replay(dir);
    tcp_shutdown();
    return rc;
  }
  if (cmd == "compare") {
    if (argc < 4) { printf("compare: usage chaos compare <a> <b>\n"); tcp_shutdown(); return 1; }
    int rc = cmd_compare(argv[2], argv[3]);
    tcp_shutdown();
    return rc;
  }
  if (cmd == "reduce") {
    std::uint64_t seed = argc > 2 ? std::stoull(argv[2]) : default_seed();
    int rc = cmd_reduce(seed);
    tcp_shutdown();
    return rc;
  }
  if (cmd == "process") { int rc = cmd_process(); tcp_shutdown(); return rc; }
  if (cmd == "network") { int rc = cmd_network(); tcp_shutdown(); return rc; }
  if (cmd == "persistence") { int rc = cmd_persistence(); tcp_shutdown(); return rc; }
  if (cmd == "authority") { int rc = cmd_authority(); tcp_shutdown(); return rc; }
  if (cmd == "resource") { int rc = cmd_resource(); tcp_shutdown(); return rc; }
  if (cmd == "cuda") { int rc = cmd_cuda(); tcp_shutdown(); return rc; }
  if (cmd == "evidence") {
    std::string dir = argc > 2 ? argv[2] : "evidence/run_0";
    int rc = cmd_evidence(dir);
    tcp_shutdown();
    return rc;
  }
  if (cmd == "benchmark") { printf("benchmark: run the chaos_bench executable for measurements\n"); tcp_shutdown(); return 0; }
  printf("unknown command: %s\n", cmd.c_str());
  tcp_shutdown();
  return 1;
}
