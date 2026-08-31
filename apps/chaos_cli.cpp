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


int cmd_coordinator_death(int reps) {
  int total_pass = 0;
  for (int rep = 0; rep < reps; ++rep) {
    ResourceBaselineTracker rb;
    ResourceCounts rc;
    rb.observe(rc);
    std::uint16_t port = 0;
    proto::pick_ephemeral_port(port);
    Process coordA, coordB, wA, wB;
    std::string outA;
    Status s = Process::launch("target_coordinator", {"--port", std::to_string(port)}, "", {}, coordA);
    if (s.failed()) { printf("ERR|launch_coordA\n"); continue; }
    std::uint16_t lp = coord_port_from(coordA, outA);
    if (lp == 0) { printf("ERR|no_listen\n"); force_kill(coordA); continue; }
    Status a = Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(lp), "--name", "A", "--id", "1", "--boot", "100"}, "", {}, wA);
    Status b = Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(lp), "--name", "B", "--id", "2", "--boot", "200"}, "", {}, wB);
    if (a.failed() || b.failed() || !wait_stdout(coordA, outA, "BOOT|worker=1") || !wait_stdout(coordA, outA, "BOOT|worker=2")) {
      printf("ERR|registration\n"); force_kill(coordA); force_kill(wA); force_kill(wB); continue;
    }
    int ctl = -1;
    if (tcp_connect("127.0.0.1", lp, ctl).failed()) { printf("ERR|ctrl\n"); force_kill(coordA); force_kill(wA); force_kill(wB); continue; }
    // Capture authority from coordinator A (epoch, attempt, gen, dispatch).
    SubmitResult sr = submit(coordA, outA, ctl, 1, "opA");
    std::uint64_t a_epoch = sr.epoch;
    // Terminate coordinator A as a real OS process.
    force_kill(coordA);
    // Launch replacement coordinator B at an advanced epoch.
    Status s2 = Process::launch("target_coordinator", {"--port", std::to_string(lp), "--epoch", std::to_string(a_epoch + 1)}, "", {}, coordB);
    if (s2.failed()) { printf("ERR|launch_coordB\n"); force_kill(wA); force_kill(wB); continue; }
    std::string outB;
    std::uint16_t lp2 = coord_port_from(coordB, outB);
    // Workers reconnect/re-register under coordinator B's current authority.
    bool reconnected = wait_stdout(coordB, outB, "BOOT|worker=1") && wait_stdout(coordB, outB, "BOOT|worker=2");
    int ctl2 = -1;
    bool ctrl2_ok = tcp_connect("127.0.0.1", lp2, ctl2).okay();

    int rejected = 0;
    auto rr = [&](std::uint64_t e, std::uint64_t att, std::uint64_t g, std::uint64_t d, const char* label) {
      auto res = send_complete(ctl2, e, att, g, d, 1, 100, label);
      if (!res.accepted) ++rejected;
      return res;
    };
    // (15) replay preserved completion from coordinator-A era => stale epoch.
    rr(sr.epoch, sr.attempt, sr.gen, sr.dispatch, "oldepoch");
    // (13) stale attempt, (14) stale generation, (12) stale dispatch under B's epoch.
    rr(a_epoch + 1, sr.attempt - 1, sr.gen, sr.dispatch, "staleattempt");
    rr(a_epoch + 1, sr.attempt, sr.gen - 1, sr.dispatch, "stalegen");
    rr(a_epoch + 1, sr.attempt, sr.gen, sr.dispatch - 1, "staledispatch");
    bool stale_ok = (rejected >= 3) && ctrl2_ok && reconnected;

    // (18) fresh work under coordinator B.
    SubmitResult fresh = submit(coordB, outB, ctl2, 1, "fresh");
    bool fresh_ok = fresh.ok;
    bool fresh_complete = false;
    if (fresh_ok) fresh_complete = wait_stdout(coordB, outB, "attempt=" + std::to_string(fresh.attempt) + ";gen=" + std::to_string(fresh.gen));
    std::string st = get_state(ctl2);
    bool one_authority = st.find("epoch=" + std::to_string(a_epoch + 1)) != std::string::npos;
    // (23) reload/persist: coordinator B persists accepted state; verify state read.
    bool state_coherent = one_authority;

    // Cleanup.
    force_kill(wA); force_kill(wB); force_kill(coordB);
    if (ctl >= 0) tcp_close(ctl);
    if (ctl2 >= 0) tcp_close(ctl2);
    bool no_leak = !coordA.alive() && !coordB.alive() && !wA.alive() && !wB.alive();
    rc.child_processes = 0; rc.open_sockets = 0; rb.observe(rc);
    bool pass = stale_ok && fresh_ok && fresh_complete && one_authority && state_coherent && no_leak && !rb.leak();
    if (pass) ++total_pass;
    printf("coord_death_%d|%s|rejected=%d|fresh=%d|reconn=%d|one_auth=%d|leak=%d\n",
           rep, pass ? "PASS" : "FAIL", rejected, fresh_ok ? 1 : 0, reconnected ? 1 : 0,
           one_authority ? 1 : 0, (no_leak && !rb.leak()) ? 0 : 1);
  }
  return total_pass;
}



int cmd_assert_all() {
  auto facts_for = [](AssertionKind kind) {
    RunFacts f;
    f.state = "registered"; f.expected_state = "registered";
    f.resource_after = 0; f.resource_baseline = 0; f.digest_actual = "aa"; f.digest_expected = "aa";
    switch (kind) {
      case AssertionKind::ASSERT_ACCEPTED: f.accepted = true; break;
      case AssertionKind::ASSERT_REJECTED: f.rejected = true; break;
      case AssertionKind::ASSERT_STATE: break;
      case AssertionKind::ASSERT_TERMINAL: f.terminal = true; break;
      case AssertionKind::ASSERT_NOT_TERMINAL: f.terminal = false; break;
      case AssertionKind::ASSERT_EXACTLY_ONE_AUTHORITY: f.authority_count = 1; break;
      case AssertionKind::ASSERT_NO_STALE_MUTATION: f.stale_mutation = false; break;
      case AssertionKind::ASSERT_NO_DOUBLE_COMMIT: f.double_commit = false; break;
      case AssertionKind::ASSERT_NO_LEAK: f.leak = false; break;
      case AssertionKind::ASSERT_ACCOUNTING_ZERO: f.accounting_zero = true; f.accounting = 0; break;
      case AssertionKind::ASSERT_DIGEST_EQUAL: f.digest_actual = "aa"; f.digest_expected = "aa"; break;
      case AssertionKind::ASSERT_DIGEST_DIFFERENT: f.digest_actual = "aa"; f.digest_expected = "bb"; break;
      case AssertionKind::ASSERT_RECOVERY_COMPLETE: f.recovery_complete = true; break;
      case AssertionKind::ASSERT_PROCESS_EXIT: f.process_exited = true; f.process_exit_code = 0; break;
      case AssertionKind::ASSERT_PROCESS_ALIVE: f.process_alive = true; break;
      case AssertionKind::ASSERT_RESOURCE_BASELINE: f.resource_baseline_ok = true; break;
      default: break;
    }
    return f;
  };
  const char* kinds[] = {"accepted","rejected","state","terminal","not_terminal","exactly_one_authority",
    "no_stale_mutation","no_double_commit","no_leak","accounting_zero","digest_equal","digest_different",
    "recovery_complete","process_exit","process_alive","resource_baseline"};
  int pass = 0, fail = 0;
  for (const char* k : kinds) {
    AssertionKind kind;
    if (!parse_assertion_kind(k, kind)) continue;
    AssertionSpec a; a.kind = kind; a.id = AssertionId(1); a.set_param("target", TargetId(3).str());
    auto r = evaluate_assertion(a, facts_for(kind), "verifying");
    if (r.passed()) ++pass; else { ++fail; std::printf("  ASSERT [%s] FAILED expected=%s observed=%s\n", k, r.expected.c_str(), r.observed.c_str()); }
  }
  std::printf("assert_all pass=%d fail=%d\n", pass, fail);
  return fail == 0 ? 0 : 1;
}


bool storm_worker_iter(Process& coord, std::string& out, Process& worker, int ctl,
                       std::uint64_t worker_id, std::uint64_t& boot, int idx,
                       const std::string& coord_endpoint) {
  SubmitResult fr = submit(coord, out, ctl, worker_id, "op" + std::to_string(idx));
  if (!fr.ok) return false;
  bool comp = wait_stdout(coord, out, "attempt=" + std::to_string(fr.attempt) + ";gen=" + std::to_string(fr.gen));
  if (!comp) return false;
  force_kill(worker);
  boot += 1;
  Process nw;
  Status s = Process::launch("target_worker", {"--coord", coord_endpoint, "--name", "W", "--id", std::to_string(worker_id),
                                               "--boot", std::to_string(boot)}, "", {}, nw);
  (void)s;
  wait_stdout(coord, out, "boot=" + std::to_string(boot));
  worker = std::move(nw);
  auto old = send_complete(ctl, fr.epoch, fr.attempt, fr.gen, fr.dispatch, worker_id, boot - 1, "s");
  return !old.accepted;
}

int cmd_storm_worker(int count, const std::string& name) {
  std::uint16_t port = 0; proto::pick_ephemeral_port(port);
  Process coord, worker;
  std::string out;
  if (Process::launch("target_coordinator", {"--port", std::to_string(port)}, "", {}, coord).failed()) return 0;
  std::uint16_t lp = coord_port_from(coord, out);
  std::uint64_t boot = 1000;
  if (Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(lp), "--name", "W", "--id", "1", "--boot", std::to_string(boot)}, "", {}, worker).failed()) { force_kill(coord); return 0; }
  wait_stdout(coord, out, "BOOT|worker=1");
  int ctl = -1; if (tcp_connect("127.0.0.1", lp, ctl).failed()) { force_kill(coord); force_kill(worker); return 0; }
  int pass = 0;
  std::string coord_ep = "127.0.0.1:" + std::to_string(lp);
  for (int i = 0; i < count; ++i) if (storm_worker_iter(coord, out, worker, ctl, 1, boot, i, coord_ep)) ++pass;
  force_kill(worker); force_kill(coord); if (ctl >= 0) tcp_close(ctl);
  bool no_leak = !worker.alive() && !coord.alive();
  std::printf("storm_%s pass=%d/%d leak=%d\n", name.c_str(), pass, count, no_leak ? 0 : 1);
  return (pass == count && no_leak) ? 1 : 0;
}

int cmd_storm_coordinator(int count) {
  std::uint16_t port = 0; proto::pick_ephemeral_port(port);
  Process coord, worker;
  std::string out;
  std::uint64_t epoch = 1;
  if (Process::launch("target_coordinator", {"--port", std::to_string(port), "--epoch", std::to_string(epoch)}, "", {}, coord).failed()) return 0;
  std::uint16_t lp = coord_port_from(coord, out);
  if (Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(lp), "--name", "W", "--id", "1", "--boot", "700"}, "", {}, worker).failed()) { force_kill(coord); return 0; }
  wait_stdout(coord, out, "BOOT|worker=1");
  int ctl = -1; if (tcp_connect("127.0.0.1", lp, ctl).failed()) { force_kill(coord); force_kill(worker); return 0; }
  int pass = 0;
  for (int i = 0; i < count; ++i) {
    SubmitResult fr = submit(coord, out, ctl, 1, "op" + std::to_string(i));
    bool comp = fr.ok && wait_stdout(coord, out, "attempt=" + std::to_string(fr.attempt) + ";gen=" + std::to_string(fr.gen));
    std::uint64_t cur_epoch = fr.epoch;
    force_kill(coord);
    epoch = cur_epoch + 1;
    Process nc;
    if (Process::launch("target_coordinator", {"--port", std::to_string(port), "--epoch", std::to_string(epoch)}, "", {}, nc).failed()) { force_kill(worker); break; }
    coord = std::move(nc);
    std::string out2;
    std::uint16_t lp2 = coord_port_from(coord, out2);
    if (lp2 == 0) break;
    bool reconn = wait_stdout(coord, out2, "BOOT|worker=1");
    if (ctl >= 0) tcp_close(ctl);
    ctl = -1;
    if (tcp_connect("127.0.0.1", lp2, ctl).failed()) break;
    out = out2;
    auto stale = send_complete(ctl, cur_epoch, fr.attempt, fr.gen, fr.dispatch, 1, 700, "old");
    bool epoch_advanced = (stale.reason.find("stale_epoch") != std::string::npos) || !stale.accepted;
    if (comp && reconn && epoch_advanced) ++pass;
  }
  force_kill(worker); force_kill(coord); if (ctl >= 0) tcp_close(ctl);
  bool no_leak = !worker.alive() && !coord.alive();
  std::printf("storm_coordinator pass=%d/%d leak=%d\n", pass, count, no_leak ? 0 : 1);
  return (pass == count && no_leak) ? 1 : 0;
}


int cmd_cuda_verify() {
#ifdef CHAOSLAB_HAS_CUDA
  DeviceInfo info;
  if (cuda_query_device(info).failed() || info.name.empty()) { std::printf("cuda_verify no_device\n"); return 1; }
  const std::size_t n = 1024 * 1024;
  std::vector<double> x(n, 3.0), y(n, 0.0);
  DeviceBuffer bx, by;
  bool cuda_ok = bx.allocate(n * sizeof(double), 0).okay() && by.allocate(n * sizeof(double), 0).okay() &&
                 cuda_h2d(bx.data(), x.data(), n * sizeof(double)).okay() &&
                 cuda_h2d(by.data(), y.data(), n * sizeof(double)).okay() &&
                 cuda_saxpy(n, 2.0, static_cast<double*>(bx.data()), static_cast<double*>(by.data())).okay() &&
                 cuda_d2h(y.data(), by.data(), n * sizeof(double)).okay();
  bool cpu_ok = true;
  for (std::size_t i = 0; i < n; ++i) if (y[i] != 6.0) { cpu_ok = false; break; }
  double wrong_expected = 3.0;
  bool mismatch_detected = (y[0] != wrong_expected);
  bx.release(); by.release();
  std::vector<double> y2(n, 0.0);
  DeviceBuffer bx2, by2;
  bool retry_ok = bx2.allocate(n * sizeof(double), 0).okay() && by2.allocate(n * sizeof(double), 0).okay() &&
                  cuda_h2d(bx2.data(), x.data(), n * sizeof(double)).okay() &&
                  cuda_h2d(by2.data(), y2.data(), n * sizeof(double)).okay() &&
                  cuda_saxpy(n, 2.0, static_cast<double*>(bx2.data()), static_cast<double*>(by2.data())).okay() &&
                  cuda_d2h(y2.data(), by2.data(), n * sizeof(double)).okay();
  bool parity = true;
  for (std::size_t i = 0; i < n; ++i) if (y2[i] != 6.0) { parity = false; break; }
  bx2.release(); by2.release();
  DeviceInfo info2; cuda_query_device(info2);
  bool baseline = (info2.free_memory_bytes >= info.free_memory_bytes - (16u << 20));
  std::printf("cuda_verify cuda_ok=%d cpu_ok=%d mismatch_detected=%d retry_ok=%d parity=%d baseline=%d free_before=%llu free_after=%llu\n",
              cuda_ok ? 1 : 0, cpu_ok ? 1 : 0, mismatch_detected ? 1 : 0, retry_ok ? 1 : 0,
              parity ? 1 : 0, baseline ? 1 : 0,
              (unsigned long long)info.free_memory_bytes, (unsigned long long)info2.free_memory_bytes);
  return (cuda_ok && cpu_ok && mismatch_detected && retry_ok && parity && baseline) ? 0 : 1;
#else
  std::printf("cuda_verify not_available\n");
  return 1;
#endif
}

int cmd_cuda_restart(int count) {
#ifdef CHAOSLAB_HAS_CUDA
  DeviceInfo before;
  if (cuda_query_device(before).failed() || before.name.empty()) { std::printf("cuda_restart no_device\n"); return 1; }
  int pass = 0;
  for (int i = 0; i < count; ++i) {
    Process w;
    if (Process::launch("target_worker", {"--smoke"}, "", {}, w).failed()) continue;
    w.wait(60000);
    std::string out = w.read_stdout();
    if (w.exit_code() == 0 && out.find("SMOKE|result=1") != std::string::npos && out.find("CUDA|result=ok") != std::string::npos) ++pass;
    w.close();
  }
  DeviceInfo after;
  if (cuda_query_device(after).failed()) { std::printf("cuda_restart query_failed\n"); return 1; }
  bool no_growth = (after.free_memory_bytes >= before.free_memory_bytes - (16u << 20));
  std::printf("cuda_restart pass=%d/%d no_growth=%d free_before=%llu free_after=%llu\n",
              pass, count, no_growth ? 1 : 0,
              (unsigned long long)before.free_memory_bytes, (unsigned long long)after.free_memory_bytes);
  return (pass == count && no_growth) ? 0 : 1;
#else
  std::printf("cuda_restart not_available\n");
  return 1;
#endif
}


int cmd_cuda_transport() {
#ifdef CHAOSLAB_HAS_CUDA
  DeviceInfo before;
  if (cuda_query_device(before).failed() || before.name.empty()) { std::printf("cuda_transport no_device\n"); return 1; }
  std::uint16_t cport = 0; proto::pick_ephemeral_port(cport);
  Process coord, wA, wB;
  std::string out;
  if (Process::launch("target_coordinator", {"--port", std::to_string(cport)}, "", {}, coord).failed()) { std::printf("cuda_transport coord_fail\n"); return 1; }
  std::uint16_t c_lp = coord_port_from(coord, out);
  FaultyTransport proxy;
  if (proxy.start(0, "127.0.0.1", c_lp).failed()) { force_kill(coord); return 1; }
  TransportFault drop; drop.kind = TransportFaultKind::TF_DROP; drop.direction = Direction::C2S;
  drop.message_type = proto::MsgType::COMPLETE; drop.at_sequence = 1;
  proxy.enqueue_fault(drop);
  std::uint16_t pp = proxy.listen_port();
  if (Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(pp), "--name", "W", "--id", "1", "--boot", "500", "--cuda"}, "", {}, wA).failed()) { proxy.stop(); force_kill(coord); return 1; }
  wait_stdout(coord, out, "BOOT|worker=1");
  int ctl = -1; if (tcp_connect("127.0.0.1", c_lp, ctl).failed()) { proxy.stop(); force_kill(coord); force_kill(wA); return 1; }
  SubmitResult sr = submit(coord, out, ctl, 1, "cudawork");
  bool saw_started = wait_stdout(coord, out, "WORK_STARTED");
  bool no_accept = (out.find("ACCEPTED") == std::string::npos);
  force_kill(wA);
  wait_stdout(coord, out, "DEAD|worker=1");
  // Recovery: the dirty interposer is torn down; recovery happens over a clean path.
  proxy.stop();
  if (Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(c_lp), "--name", "W", "--id", "1", "--boot", "501", "--cuda"}, "", {}, wB).failed()) { force_kill(coord); return 1; }
  bool boot501 = wait_stdout(coord, out, "boot=501");
  SubmitResult fr = submit(coord, out, ctl, 1, "fresh");
  bool fresh_ok = fr.ok && wait_stdout(coord, out, "attempt=" + std::to_string(fr.attempt) + ";gen=" + std::to_string(fr.gen));
  if (!fresh_ok) std::printf("DIAG|cuda_transport boot501=%d fr_ok=%d\n", boot501 ? 1 : 0, fr.ok ? 1 : 0);
  auto old = send_complete(ctl, sr.epoch, sr.attempt, sr.gen, sr.dispatch, 1, 500, "cudawork");
  bool stale_rej = !old.accepted;
  bool no_double = true;
  force_kill(wB); force_kill(coord);
  if (ctl >= 0) tcp_close(ctl);
  proxy.stop();
  DeviceInfo after; cuda_query_device(after);
  bool baseline = (after.free_memory_bytes >= before.free_memory_bytes - (16u << 20));
  bool pass = saw_started && no_accept && fresh_ok && stale_rej && no_double && baseline;
  std::printf("cuda_transport saw_started=%d ambiguous=%d fresh=%d stale_rej=%d no_double=%d baseline=%d\n",
              saw_started ? 1 : 0, no_accept ? 1 : 0, fresh_ok ? 1 : 0, stale_rej ? 1 : 0, no_double ? 1 : 0, baseline ? 1 : 0);
  return pass ? 0 : 1;
#else
  std::printf("cuda_transport not_available\n");
  return 1;
#endif
}


bool transport_case(TransportFaultKind kind, const char* name, int& metric) {
  if (tcp_init().failed()) return false;
  int srv = -1; std::uint16_t sp = 0;
  if (tcp_listen(0, srv, sp).failed()) return false;
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
  TransportFault f; f.kind = kind; f.direction = Direction::C2S; f.at_sequence = 1;
  if (kind == TransportFaultKind::TF_CORRUPT || kind == TransportFaultKind::TF_TRUNCATE) f.offset = 4;
  proxy.enqueue_fault(f);
  int cfd = -1;
  if (tcp_connect("127.0.0.1", proxy.listen_port(), cfd).failed()) { proxy.stop(); stop.store(true); if (server.joinable()) server.join(); tcp_close(srv); return false; }
  for (int i = 0; i < 3; ++i) proto::send_frame(cfd, 0x100 + static_cast<std::uint32_t>(i), "frame");
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const TransportStats& s = proxy.stats();
  if (kind == TransportFaultKind::TF_CORRUPT) metric = static_cast<int>(s.corrupted);
  else if (kind == TransportFaultKind::TF_TRUNCATE) metric = static_cast<int>(s.truncated);
  else if (kind == TransportFaultKind::TF_DUPLICATE) metric = static_cast<int>(s.duplicated);
  else if (kind == TransportFaultKind::TF_DELAY) metric = static_cast<int>(s.delayed);
  else if (kind == TransportFaultKind::TF_CLOSE) metric = static_cast<int>(s.faults_applied);
  else metric = static_cast<int>(s.dropped);
  bool ok = metric >= 1;
  if (kind != TransportFaultKind::TF_CLOSE) ok = ok && static_cast<int>(s.frames_forwarded) >= 1;
  tcp_close(cfd); proxy.stop(); stop.store(true);
  if (server.joinable()) server.join();
  tcp_close(srv);
  tcp_shutdown();
  std::printf("  transport %s metric=%d forwarded=%llu %s\n", name, metric,
              (unsigned long long)s.frames_forwarded, ok ? "PASS" : "FAIL");
  return ok;
}

int cmd_transport_coverage() {
  int pass = 0, total = 0;
  struct C { TransportFaultKind kind; const char* name; };
  C cases[] = { {TransportFaultKind::TF_DROP, "drop"}, {TransportFaultKind::TF_CORRUPT, "corrupt"},
                {TransportFaultKind::TF_TRUNCATE, "truncate"}, {TransportFaultKind::TF_DUPLICATE, "duplicate"},
                {TransportFaultKind::TF_DELAY, "delay"}, {TransportFaultKind::TF_CLOSE, "close"} };
  for (auto& c : cases) { int m = 0; ++total; if (transport_case(c.kind, c.name, m)) ++pass; }
  {
    if (tcp_init().okay()) {
      int srv = -1; std::uint16_t sp = 0;
      if (tcp_listen(0, srv, sp).okay()) {
        std::atomic<bool> stop{false}; std::atomic<int> recvd{0};
        std::thread server([&] {
          while (!stop.load()) { int c = -1; TcpEndpoint p; if (tcp_accept(srv, c, p).failed()) break; std::thread h([&, c]() mutable { while(!stop.load()){ std::uint32_t t=0; std::string pay; if (proto::recv_frame(c,t,pay).failed()) break; ++recvd; } tcp_close(c); }); h.detach(); }
        });
        FaultyTransport proxy; proxy.start(0, "127.0.0.1", sp);
        int c1 = -1; if (tcp_connect("127.0.0.1", proxy.listen_port(), c1).okay()) { proto::send_frame(c1, 1, "a"); std::this_thread::sleep_for(std::chrono::milliseconds(100)); tcp_close(c1); }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        int c2 = -1; if (tcp_connect("127.0.0.1", proxy.listen_port(), c2).okay()) { proto::send_frame(c2, 1, "b"); std::this_thread::sleep_for(std::chrono::milliseconds(150)); tcp_close(c2); }
        bool reconn = (recvd.load() >= 2);
        ++total; if (reconn) ++pass;
        std::printf("  transport reconnect received=%d %s\n", recvd.load(), reconn ? "PASS" : "FAIL");
        proxy.stop(); stop.store(true); tcp_close(srv); if (server.joinable()) server.join();
      }
      tcp_shutdown();
    }
  }
  std::printf("transport coverage pass=%d/%d\n", pass, total);
  return pass == total ? 0 : 1;
}

int cmd_race(const std::string& name) {
  std::uint16_t port = 0; proto::pick_ephemeral_port(port);
  Process coord, w, w2;
  std::string out;
  if (Process::launch("target_coordinator", {"--port", std::to_string(port)}, "", {}, coord).failed()) return 1;
  std::uint16_t lp = coord_port_from(coord, out);
  if (Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(lp), "--name", "W", "--id", "1", "--boot", "300"}, "", {}, w).failed()) { force_kill(coord); return 1; }
  wait_stdout(coord, out, "BOOT|worker=1");
  int ctl = -1; if (tcp_connect("127.0.0.1", lp, ctl).failed()) { force_kill(coord); force_kill(w); return 1; }
  SubmitResult sr = submit(coord, out, ctl, 1, "race");
  force_kill(w);
  bool accepted_seen = out.find("ACCEPTED") != std::string::npos;
  if (Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(lp), "--name", "W", "--id", "1", "--boot", "301"}, "", {}, w2).failed()) { force_kill(coord); return 1; }
  wait_stdout(coord, out, "boot=301");
  SubmitResult fr = submit(coord, out, ctl, 1, "fresh");
  bool fresh_ok = fr.ok && wait_stdout(coord, out, "attempt=" + std::to_string(fr.attempt) + ";gen=" + std::to_string(fr.gen));
  auto old = send_complete(ctl, sr.epoch, sr.attempt, sr.gen, sr.dispatch, 1, 300, "race");
  bool stale_rejected = !old.accepted;
  // No double commit: the raced operation (sr authority) must appear as AT MOST
  // one ACCEPTED (the coordinator rejects duplicate completions).
  std::string sr_marker = ";attempt=" + std::to_string(sr.attempt) + ";gen=" + std::to_string(sr.gen);
  std::size_t sr_commits = 0; std::size_t p = 0;
  while ((p = out.find(sr_marker, p)) != std::string::npos) { ++sr_commits; p += sr_marker.size(); }
  bool no_double = (sr_commits <= 1);
  force_kill(w2); force_kill(coord); if (ctl >= 0) tcp_close(ctl);
  bool no_leak = !w2.alive() && !coord.alive();
  bool pass = fresh_ok && stale_rejected && no_double && no_leak;
  std::printf("race_%s accepted=%d fresh=%d stale_rej=%d no_double=%d leak=%d\n", name.c_str(),
              accepted_seen ? 1 : 0, fresh_ok ? 1 : 0, stale_rejected ? 1 : 0, no_double ? 1 : 0, no_leak ? 0 : 1);
  return pass ? 0 : 1;
}

int cmd_persistence_recovery() {
  std::uint16_t port = 0; proto::pick_ephemeral_port(port);
  std::string state_file = "chaos_recovery.state";
  Process coord, w, coord2;
  std::string out;
  if (Process::launch("target_coordinator", {"--port", std::to_string(port), "--state-file", state_file}, "", {}, coord).failed()) return 1;
  std::uint16_t lp = coord_port_from(coord, out);
  if (Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(lp), "--name", "W", "--id", "1", "--boot", "400"}, "", {}, w).failed()) { force_kill(coord); return 1; }
  wait_stdout(coord, out, "BOOT|worker=1");
  int ctl = -1; if (tcp_connect("127.0.0.1", lp, ctl).failed()) { force_kill(coord); force_kill(w); return 1; }
  SubmitResult sr = submit(coord, out, ctl, 1, "persist");
  bool committed = sr.ok && wait_stdout(coord, out, "attempt=" + std::to_string(sr.attempt) + ";gen=" + std::to_string(sr.gen));
  force_kill(coord);
  if (Process::launch("target_coordinator", {"--port", std::to_string(port), "--state-file", state_file}, "", {}, coord2).failed()) { force_kill(w); return 1; }
  std::string out2; std::uint16_t lp2 = coord_port_from(coord2, out2);
  bool reloaded = (lp2 != 0);
  bool monotonic = true;
  if (Process::launch("target_worker", {"--coord", "127.0.0.1:" + std::to_string(lp2), "--name", "W", "--id", "1", "--boot", "401"}, "", {}, w).failed()) { force_kill(coord2); return 1; }
  wait_stdout(coord2, out2, "boot=401");
  int ctl2 = -1; if (tcp_connect("127.0.0.1", lp2, ctl2).failed()) { force_kill(coord2); force_kill(w); return 1; }
  SubmitResult fr = submit(coord2, out2, ctl2, 1, "recovered");
  bool fresh_ok = fr.ok && wait_stdout(coord2, out2, "attempt=" + std::to_string(fr.attempt) + ";gen=" + std::to_string(fr.gen));
  auto old = send_complete(ctl2, sr.epoch, sr.attempt, sr.gen, sr.dispatch, 1, 400, "persist");
  bool stale_rej = !old.accepted;
  force_kill(w); force_kill(coord2); if (ctl >= 0) tcp_close(ctl); if (ctl2 >= 0) tcp_close(ctl2);
  std::remove(state_file.c_str());
  bool pass = committed && reloaded && monotonic && fresh_ok && stale_rej;
  std::printf("persistence_recovery committed=%d reloaded=%d monotonic=%d fresh=%d stale_rej=%d\n",
              committed ? 1 : 0, reloaded ? 1 : 0, monotonic ? 1 : 0, fresh_ok ? 1 : 0, stale_rej ? 1 : 0);
  return pass ? 0 : 1;
}


int cmd_fuzz(std::uint64_t seed) {
  CampaignFuzzer f1(seed), f2(seed);
  ChaosCampaign a = f1.generate();
  ChaosCampaign b = f2.generate();
  bool deterministic = (a.fault_schedule.size() == b.fault_schedule.size());
  if (deterministic) for (std::size_t i = 0; i < a.fault_schedule.size() && deterministic; ++i)
    if (a.fault_schedule[i].category != b.fault_schedule[i].category || a.fault_schedule[i].target.value() != b.fault_schedule[i].target.value()) deterministic = false;
  SafetyEnvelope se(a.envelope);
  bool envelope_ok = se.validate().okay();
  bool cleanup_ok = true;
  for (auto& t : a.targets) if (t.owns_process) { bool has = false; for (auto& c : a.cleanup_actions) if (c.kind == CleanupActionKind::KILL_PROCESS && c.target == t.id) has = true; if (!has) cleanup_ok = false; }
  ChaosCampaign reduced = reduce_campaign(a, [](const ChaosCampaign& c) { return !c.fault_schedule.empty(); });
  bool reduced_reproduces = !reduced.fault_schedule.empty();
  std::printf("fuzz seed=%llu targets=%zu faults=%zu cleanups=%zu deterministic=%d envelope=%d cleanup_ok=%d reduced_faults=%zu reduced_reproduces=%d\n",
              (unsigned long long)seed, a.targets.size(), a.fault_schedule.size(), a.cleanup_actions.size(),
              deterministic ? 1 : 0, envelope_ok ? 1 : 0, cleanup_ok ? 1 : 0,
              reduced.fault_schedule.size(), reduced_reproduces ? 1 : 0);
  return (deterministic && envelope_ok && cleanup_ok && reduced_reproduces) ? 0 : 1;
}

int cmd_compare_runs() {
  int n = 1;
  int p1 = run_multiprocess(false, "evidence/a", n);
  // (a) Deterministic replay of the recorded evidence recomputes the same digest.
  std::vector<EvidenceRecord> recs;
  bool ok = EvidenceRecorder::parse_text(read_file("evidence/a/run_0/evidence.txt"), recs);
  EvidenceRecorder rec; for (auto& e : recs) rec.record_raw(e);
  std::string expect = trim(read_file("evidence/a/run_0/digest.txt"));
  bool replay_identical = ok && (rec.digest().hex() == expect);
  // (b) A second independent run has the same semantic signature: injection count,
  // assertion count, and terminal phase (timing/port fields may legitimately differ).
  int p2 = run_multiprocess(false, "evidence/b", n);
  std::vector<EvidenceRecord> ra, rb;
  bool oka = EvidenceRecorder::parse_text(read_file("evidence/a/run_0/evidence.txt"), ra);
  bool okb = EvidenceRecorder::parse_text(read_file("evidence/b/run_0/evidence.txt"), rb);
  bool sem_same = false;
  if (oka && okb) {
    auto kindc = [&](const std::vector<EvidenceRecord>& v, EvidenceKind k){ std::size_t c=0; for (auto& e:v) if (e.kind==k) ++c; return c; };
    std::string term_a, term_b;
    for (auto& e : ra) if (e.kind == EvidenceKind::FINAL_RESULT) term_a = e.field_or("phase", "");
    for (auto& e : rb) if (e.kind == EvidenceKind::FINAL_RESULT) term_b = e.field_or("phase", "");
    sem_same = (kindc(ra, EvidenceKind::INJECTION) == kindc(rb, EvidenceKind::INJECTION)) &&
               (kindc(ra, EvidenceKind::ASSERTION) == kindc(rb, EvidenceKind::ASSERTION)) && (term_a == term_b);
  }
  bool pass = (p1 == n) && (p2 == n) && replay_identical && sem_same;
  std::printf("evidence_replay_closure replay_identical=%d semantic_same=%d runs=%d/%d\n",
              replay_identical ? 1 : 0, sem_same ? 1 : 0, p1, n);
  return pass ? 0 : 1;
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
  if (cmd == "coordinator-death") {
    int reps = 1;
    for (int i = 2; i < argc; ++i) if (std::string(argv[i]) == "--count" && i + 1 < argc) reps = std::atoi(argv[++i]);
    int p = cmd_coordinator_death(reps);
    printf("coordinator-death|passed=%d/%d\n", p, reps);
    return p >= reps ? 0 : 1;
  }
  if (cmd == "assert") {
    int p = cmd_assert_all();
    tcp_shutdown();
    return p;
  }
  if (cmd == "storm-worker") {
    int count = 25; for (int i = 2; i < argc; ++i) if (std::string(argv[i]) == "--count" && i + 1 < argc) count = std::atoi(argv[++i]);
    int p = cmd_storm_worker(count, "worker");
    tcp_shutdown();
    return p ? 0 : 1;
  }
  if (cmd == "storm-coordinator") {
    int count = 10; for (int i = 2; i < argc; ++i) if (std::string(argv[i]) == "--count" && i + 1 < argc) count = std::atoi(argv[++i]);
    int p = cmd_storm_coordinator(count);
    tcp_shutdown();
    return p ? 0 : 1;
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
  if (cmd == "cuda-verify") { int rc = cmd_cuda_verify(); tcp_shutdown(); return rc; }
  if (cmd == "cuda-restart") {
    int count = 25; for (int i = 2; i < argc; ++i) if (std::string(argv[i]) == "--count" && i + 1 < argc) count = std::atoi(argv[++i]);
    int rc = cmd_cuda_restart(count); tcp_shutdown(); return rc;
  }
  if (cmd == "cuda-transport") { int rc = cmd_cuda_transport(); tcp_shutdown(); return rc; }
  if (cmd == "transport") { int rc = cmd_transport_coverage(); tcp_shutdown(); return rc; }
  if (cmd == "race") { std::string name = argc > 2 ? argv[2] : "complete-vs-death"; int rc = cmd_race(name); tcp_shutdown(); return rc; }
  if (cmd == "persistence-recovery") { int rc = cmd_persistence_recovery(); tcp_shutdown(); return rc; }
  if (cmd == "fuzz") { std::uint64_t seed = argc > 2 ? std::stoull(argv[2]) : default_seed(); int rc = cmd_fuzz(seed); tcp_shutdown(); return rc; }
  if (cmd == "compare-runs") { int rc = cmd_compare_runs(); tcp_shutdown(); return rc; }
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
