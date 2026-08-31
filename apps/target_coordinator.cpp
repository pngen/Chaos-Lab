#include "chaoslab/runtime_protocol.h"
#include "chaoslab/tcp.h"
#include "chaoslab/text.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

using namespace chaoslab;

namespace {

struct Worker {
  std::uint64_t id{0};
  std::uint64_t boot{0};
  std::string name;
  int fd{-1};
  bool alive{false};
  std::mutex send_mtx;
};

struct CoordState {
  std::uint64_t epoch{1};
  std::uint64_t next_attempt{0};
  std::uint64_t next_gen{0};
  std::uint64_t next_dispatch{0};
  std::uint64_t cur_attempt{0};
  std::uint64_t cur_gen{0};
  std::uint64_t cur_dispatch{0};
  std::uint64_t cur_boot{0};
  std::uint64_t cur_worker{0};
  bool has_submitted{false};
  std::string state_file;
  std::map<std::uint64_t, Worker> workers;
  std::map<int, std::uint64_t> fd_to_worker;
  std::set<std::string> committed;
  std::map<std::string, std::string> sig_result;
  bool shutting_down{false};
};

CoordState g_state;
std::mutex g_mtx;

std::string authority_text_locked() {
  return "epoch=" + std::to_string(g_state.epoch) +
         ";attempt=" + std::to_string(g_state.cur_attempt) +
         ";gen=" + std::to_string(g_state.cur_gen) +
         ";dispatch=" + std::to_string(g_state.cur_dispatch) +
         ";boot=" + std::to_string(g_state.cur_boot) +
         ";worker=" + std::to_string(g_state.cur_worker);
}

void report(const char* tag, const std::string& detail) {
  printf("%s|%s\n", tag, detail.c_str());
  fflush(stdout);
}

void save_state() {
  if (g_state.state_file.empty()) return;
  std::lock_guard<std::mutex> lk(g_mtx);
  std::string s = authority_text_locked();
  s += "\ncommitted=" + std::to_string(g_state.committed.size());
  std::string tmp = g_state.state_file + ".tmp";
  FILE* f = nullptr;
  if (fopen_s(&f, tmp.c_str(), "wb") == 0 && f) {
    std::fwrite(s.data(), 1, s.size(), f);
    std::fclose(f);
    std::rename(tmp.c_str(), g_state.state_file.c_str());
  }
}

bool current_authority(std::uint64_t epoch, std::uint64_t boot, std::uint64_t attempt,
                       std::uint64_t gen, std::uint64_t dispatch) {
  return g_state.has_submitted && epoch == g_state.epoch && boot == g_state.cur_boot &&
         attempt == g_state.cur_attempt && gen == g_state.cur_gen && dispatch == g_state.cur_dispatch;
}

void handle_register(const std::string& payload, int fd) {
  auto parts = split(payload, '|');
  if (parts.size() != 3) return;
  std::uint64_t id = std::stoull(parts[1]);
  std::uint64_t boot = std::stoull(parts[2]);
  std::uint64_t epoch;
  bool fresh;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto& w = g_state.workers[id];
    fresh = (w.fd < 0) || (w.boot != boot);
    w.id = id; w.boot = boot; w.name = parts[0]; w.fd = fd; w.alive = true;
    g_state.fd_to_worker[fd] = id;
    epoch = g_state.epoch;
  }
  if (fresh) report("BOOT", "worker=" + std::to_string(id) + ";boot=" + std::to_string(boot) + ";fresh=1");
  proto::send_frame(fd, proto::MsgType::REGISTERED, std::to_string(epoch));
}

void handle_complete(const std::string& payload, int fd) {
  auto parts = split(payload, '|');
  if (parts.size() != 7) {
    report("REJECTED", "malformed_complete");
    proto::send_frame(fd, proto::MsgType::COMPLETE_REJECTED, "malformed");
    return;
  }
  std::uint64_t req_epoch = std::stoull(parts[0]);
  std::uint64_t req_attempt = std::stoull(parts[1]);
  std::uint64_t req_gen = std::stoull(parts[2]);
  std::uint64_t req_dispatch = std::stoull(parts[3]);
  std::uint64_t req_worker = std::stoull(parts[4]);
  std::uint64_t req_boot = std::stoull(parts[5]);
  std::string result = parts[6];

  std::string reject = "";
  std::string accept_detail;
  std::lock_guard<std::mutex> lk(g_mtx);
  if (req_epoch != g_state.epoch) reject = "stale_epoch";
  else if (req_boot != g_state.cur_boot) reject = "stale_boot";
  else if (req_attempt != g_state.cur_attempt) reject = "stale_attempt";
  else if (req_gen != g_state.cur_gen) reject = "stale_generation";
  else if (req_dispatch != g_state.cur_dispatch) reject = "stale_dispatch";
  else if (!current_authority(req_epoch, req_boot, req_attempt, req_gen, req_dispatch)) reject = "not_current_authority";

  std::string sig = std::to_string(req_epoch) + ":" + std::to_string(req_attempt) + ":" +
                    std::to_string(req_gen) + ":" + std::to_string(req_dispatch) + ":" +
                    std::to_string(req_worker) + ":" + std::to_string(req_boot);
  if (reject.empty()) {
    if (g_state.committed.count(sig)) {
      bool conflict = g_state.sig_result[sig] != result;
      reject = conflict ? "conflicting_duplicate" : "duplicate_current_completion";
    }
  }
  if (!reject.empty()) {
    report("REJECTED", reject);
    proto::send_frame(fd, proto::MsgType::COMPLETE_REJECTED, reject);
    return;
  }
  g_state.committed.insert(sig);
  g_state.sig_result[sig] = result;
  // Keep has_submitted true so a repeat of the current authority is detected as
  // a duplicate/conflict rather than "not_current_authority".
  // g_state.has_submitted is intentionally left true.
  accept_detail = "epoch=" + std::to_string(g_state.epoch) + ";attempt=" + std::to_string(g_state.cur_attempt) +
                  ";gen=" + std::to_string(g_state.cur_gen) + ";dispatch=" + std::to_string(g_state.cur_dispatch) +
                  ";result=" + result;
  report("ACCEPTED", accept_detail);
  proto::send_frame(fd, proto::MsgType::COMPLETE_ACK, "committed");
  // Save after releasing? Just record; keep simple.
}

void handle_submit(const std::string& payload, int fd) {
  auto parts = split(payload, '|');
  if (parts.size() != 2) { proto::send_frame(fd, proto::MsgType::COMPLETE_REJECTED, "bad_submit"); return; }
  std::uint64_t target_worker = std::stoull(parts[0]);
  std::string work = parts[1];

  int worker_fd = -1;
  std::mutex* wm = nullptr;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_state.workers.find(target_worker);
    if (it == g_state.workers.end() || !it->second.alive) {
      proto::send_frame(fd, proto::MsgType::COMPLETE_REJECTED, "worker_not_alive");
      return;
    }
    g_state.next_attempt += 1;
    g_state.next_gen += 1;
    g_state.next_dispatch += 1;
    g_state.cur_attempt = g_state.next_attempt;
    g_state.cur_gen = g_state.next_gen;
    g_state.cur_dispatch = g_state.next_dispatch;
    g_state.cur_boot = it->second.boot;
    g_state.cur_worker = target_worker;
    g_state.has_submitted = true;
    worker_fd = it->second.fd;
    wm = &it->second.send_mtx;
  }
  std::string dispatch_payload = std::to_string(g_state.epoch) + "|" + std::to_string(g_state.cur_attempt) + "|" +
                                 std::to_string(g_state.cur_gen) + "|" + std::to_string(g_state.cur_dispatch) + "|" + work;
  wm->lock();
  bool sent = proto::send_frame(worker_fd, proto::MsgType::DISPATCH, dispatch_payload).okay();
  wm->unlock();
  std::string resp = std::to_string(g_state.epoch) + "|" + std::to_string(g_state.cur_attempt) + "|" +
                     std::to_string(g_state.cur_gen) + "|" + std::to_string(g_state.cur_dispatch);
  proto::send_frame(fd, proto::MsgType::SUBMIT_RESPONSE, resp);
  if (!sent) report("WARN", "dispatch_to_worker_failed");
}

void handle_get_state(int fd) {
  std::string s;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    s = authority_text_locked();
    s += "\ncommitted=" + std::to_string(g_state.committed.size());
    s += "\nworkers=" + std::to_string(g_state.workers.size());
  }
  proto::send_frame(fd, proto::MsgType::STATE, s);
}

void connection_thread(int fd) {
  while (true) {
    std::uint32_t type = 0;
    std::string payload;
    Status s = proto::recv_frame(fd, type, payload);
    if (s.failed()) break;
    switch (type) {
      case proto::MsgType::REGISTER: handle_register(payload, fd); break;
      case proto::MsgType::SUBMIT: handle_submit(payload, fd); break;
      case proto::MsgType::COMPLETE: handle_complete(payload, fd); break;
      case proto::MsgType::GET_STATE: handle_get_state(fd); break;
      case proto::MsgType::WORK_STARTED: report("WORK_STARTED", payload); break;
      case proto::MsgType::PING: proto::send_frame(fd, proto::MsgType::PONG, "pong"); break;
      case proto::MsgType::SHUTDOWN:
        { std::lock_guard<std::mutex> lk(g_mtx); g_state.shutting_down = true; }
        proto::send_frame(fd, proto::MsgType::PONG, "bye");
        return;
      default: break;
    }
  }
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_state.fd_to_worker.find(fd);
    if (it != g_state.fd_to_worker.end()) {
      auto wit = g_state.workers.find(it->second);
      if (wit != g_state.workers.end()) {
        wit->second.alive = false;
        report("DEAD", "worker=" + std::to_string(it->second) + ";boot=" + std::to_string(wit->second.boot));
      }
      g_state.fd_to_worker.erase(it);
    }
  }
  tcp_close(fd);
}

} // namespace

int main(int argc, char** argv) {
  std::uint16_t port = 0;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--port" && i + 1 < argc) port = static_cast<std::uint16_t>(std::atoi(argv[++i]));
    else if (a == "--state-file" && i + 1 < argc) { std::lock_guard<std::mutex> lk(g_mtx); g_state.state_file = argv[++i]; }
    else if (a == "--epoch" && i + 1 < argc) { std::lock_guard<std::mutex> lk(g_mtx); g_state.epoch = std::stoull(argv[++i]); }
  }
  if (tcp_init().failed()) { printf("ERR|tcp_init\n"); return 2; }
  int srv = -1; std::uint16_t bound = 0;
  if (tcp_listen(port, srv, bound).failed()) { printf("ERR|listen\n"); return 3; }
  printf("LISTEN|port=%u\n", (unsigned)bound); fflush(stdout);

  std::atomic<bool> running{true};
  std::vector<std::thread> threads;
  while (running.load()) {
    int fd = -1; TcpEndpoint peer;
    Status as = tcp_accept(srv, fd, peer);
    if (as.failed()) break;
    threads.emplace_back(connection_thread, fd);
    if (threads.size() > 64) { // periodical reap
      for (auto& t : threads) t.join();
      threads.clear();
      break;
    }
  }
  for (auto& t : threads) t.join();
  tcp_close(srv);
  tcp_shutdown();
  return 0;
}
