#include "chaoslab/transport.h"

#include "chaoslab/tcp.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>

namespace chaoslab {

std::vector<std::uint8_t> Frame::encode() const {
  std::vector<std::uint8_t> out;
  out.reserve(HEADER + payload.size());
  auto put32 = [&](std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xff));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xff));
  };
  put32(MAGIC);
  put32(type);
  put32(static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

bool Frame::decode_header(const std::uint8_t* src, std::size_t n, std::uint32_t& type, std::uint32_t& len) {
  if (n < HEADER) return false;
  auto get32 = [&](std::size_t off) {
    return static_cast<std::uint32_t>(src[off]) |
           (static_cast<std::uint32_t>(src[off + 1]) << 8) |
           (static_cast<std::uint32_t>(src[off + 2]) << 16) |
           (static_cast<std::uint32_t>(src[off + 3]) << 24);
  };
  if (get32(0) != MAGIC) return false;
  type = get32(4);
  len = get32(8);
  return true;
}

bool Frame::decode(const std::uint8_t* src, std::size_t n, Frame& out, std::size_t& consumed) {
  consumed = 0;
  if (n < HEADER) return false;
  auto get32 = [&](std::size_t off) {
    return static_cast<std::uint32_t>(src[off]) |
           (static_cast<std::uint32_t>(src[off + 1]) << 8) |
           (static_cast<std::uint32_t>(src[off + 2]) << 16) |
           (static_cast<std::uint32_t>(src[off + 3]) << 24);
  };
  if (get32(0) != MAGIC) return false;
  out.type = get32(4);
  std::uint32_t len = get32(8);
  if (n < HEADER + len) return false;
  out.payload.assign(src + HEADER, src + HEADER + len);
  consumed = HEADER + len;
  return true;
}

const char* direction_name(Direction d) noexcept {
  return d == Direction::C2S ? "c2s" : "s2c";
}

const char* transport_fault_name(TransportFaultKind k) noexcept {
  switch (k) {
    case TransportFaultKind::TF_DROP: return "drop";
    case TransportFaultKind::TF_DUPLICATE: return "duplicate";
    case TransportFaultKind::TF_TRUNCATE: return "truncate";
    case TransportFaultKind::TF_CORRUPT: return "corrupt";
    case TransportFaultKind::TF_REORDER: return "reorder";
    case TransportFaultKind::TF_DELAY: return "delay";
    case TransportFaultKind::TF_HALF_CLOSE: return "half_close";
    case TransportFaultKind::TF_CLOSE: return "close";
  }
  return "unknown";
}

FaultyTransport::~FaultyTransport() { stop(); }

Status FaultyTransport::start(std::uint16_t listen_port, const std::string& target_host, std::uint16_t target_port) {
  if (running_.load()) return Status::error(StatusCode::already_exists, "transport already running");
  target_host_ = target_host;
  target_port_ = target_port;
  std::uint16_t bound = 0;
  int fd = -1;
  Status s = tcp_listen(listen_port, fd, bound);
  if (s.failed()) return s;
  server_fd_.store(fd);
  listen_port_ = bound;
  running_.store(true);
  accept_thread_ = std::thread([this] { accept_loop(); });
  return Status::ok();
}

void FaultyTransport::stop() noexcept {
  bool was = running_.exchange(false);
  int fd = server_fd_.exchange(-1);
  if (fd >= 0) tcp_close(fd);
  if (accept_thread_.joinable()) accept_thread_.join();
  (void)was;
}

void FaultyTransport::enqueue_fault(TransportFault f) { faults_.push_back(std::move(f)); }

namespace {

Status read_exact(int fd, std::uint8_t* buf, std::size_t n, bool& closed) {
  std::size_t got = 0;
  while (got < n) {
    std::size_t recvd = 0;
    Status s = tcp_recv_some(fd, buf + got, n - got, recvd);
    if (s.failed()) return s;
    if (recvd == 0) { closed = true; return Status::ok(); }
    got += recvd;
  }
  return Status::ok();
}

bool forward_frames(int in_fd, int out_fd, Direction dir,
                    const std::vector<TransportFault>& local_faults,
                    TransportStats& stats,
                    std::atomic<bool>& aborted) {
  tcp_set_recv_timeout(in_fd, 50);
  std::uint64_t seq = 0;
  std::vector<std::uint8_t> pending;
  std::uint8_t header[Frame::HEADER];

  while (true) {
    if (aborted.load()) return true;
    bool closed = false;
    Status s = read_exact(in_fd, header, Frame::HEADER, closed);
    if (s.failed()) { aborted.store(true); return true; }
    if (closed) { aborted.store(true); return true; }

    std::uint32_t mtype = 0;
    std::uint32_t payload_len = 0;
    if (!Frame::decode_header(header, Frame::HEADER, mtype, payload_len)) { aborted.store(true); return true; }
    std::vector<std::uint8_t> bytes(Frame::HEADER + payload_len);
    std::memcpy(bytes.data(), header, Frame::HEADER);
    if (payload_len > 0) {
      std::vector<std::uint8_t> p(payload_len);
      if (read_exact(in_fd, p.data(), payload_len, closed).failed() || closed) { aborted.store(true); return true; }
      std::memcpy(&bytes[Frame::HEADER], p.data(), payload_len);
    }
    ++seq;

    const TransportFault* applied = nullptr;
    for (auto& tf : local_faults) {
      if (tf.direction != dir) continue;
      if (tf.message_type != 0 && tf.message_type != mtype) continue;
      if (tf.at_sequence != 0 && tf.at_sequence != seq) continue;
      applied = &tf;
      break;
    }

    auto forward = [&](const std::vector<std::uint8_t>& data, int count) {
      for (int i = 0; i < count; ++i) {
        std::size_t sent = 0;
        tcp_send_all(out_fd, data.data(), data.size(), sent);
        ++stats.frames_forwarded;
      }
    };

    if (applied) {
      ++stats.faults_applied;
      switch (applied->kind) {
        case TransportFaultKind::TF_DROP: ++stats.dropped; continue;
        case TransportFaultKind::TF_DUPLICATE:
          ++stats.duplicated;
          forward(bytes, 2);
          continue;
        case TransportFaultKind::TF_TRUNCATE: {
          ++stats.truncated;
          std::size_t cut = static_cast<std::size_t>(applied->offset);
          if (cut < Frame::HEADER) cut = Frame::HEADER;
          if (cut > bytes.size()) cut = bytes.size();
          std::uint32_t newlen = static_cast<std::uint32_t>(cut - Frame::HEADER);
          bytes[8] = static_cast<std::uint8_t>(newlen & 0xff);
          bytes[9] = static_cast<std::uint8_t>((newlen >> 8) & 0xff);
          bytes[10] = static_cast<std::uint8_t>((newlen >> 16) & 0xff);
          bytes[11] = static_cast<std::uint8_t>((newlen >> 24) & 0xff);
          bytes.resize(cut);
          forward(bytes, 1);
          continue;
        }
        case TransportFaultKind::TF_CORRUPT: {
          ++stats.corrupted;
          int off = applied->offset;
          if (off >= static_cast<int>(Frame::HEADER) && off < static_cast<int>(bytes.size()))
            bytes[static_cast<std::size_t>(off)] ^= 0xa5;
          forward(bytes, 1);
          continue;
        }
        case TransportFaultKind::TF_REORDER: {
          ++stats.reordered;
          if (!pending.empty()) {
            std::vector<std::uint8_t> tmp = pending;
            pending.assign(bytes.begin(), bytes.end());
            forward(tmp, 1);
          } else {
            pending.assign(bytes.begin(), bytes.end());
          }
          continue;
        }
        case TransportFaultKind::TF_DELAY:
          ++stats.delayed;
          std::this_thread::sleep_for(std::chrono::milliseconds(40));
          forward(bytes, 1);
          continue;
        case TransportFaultKind::TF_HALF_CLOSE:
        case TransportFaultKind::TF_CLOSE:
          aborted.store(true);
          return false;
      }
    }
    forward(bytes, 1);
  }
}
} // namespace

void FaultyTransport::accept_loop() {
  while (running_.load()) {
    int client_fd = -1;
    TcpEndpoint peer;
    Status s = tcp_accept(server_fd_.load(), client_fd, peer);
    if (s.failed()) {
      if (running_.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      else break;
      continue;
    }
    int target_fd = -1;
    Status cs = tcp_connect(target_host_, target_port_, target_fd);
    if (cs.failed()) { tcp_close(client_fd); continue; }
    run_connection(client_fd, target_fd);
  }
}

void FaultyTransport::run_connection(int client_fd, int target_fd) {
  std::vector<TransportFault> local_faults = faults_;
  TransportStats stats;
  std::atomic<bool> aborted{false};
  std::thread c2s([&] { forward_frames(client_fd, target_fd, Direction::C2S, local_faults, stats, aborted); });
  std::thread s2c([&] { forward_frames(target_fd, client_fd, Direction::S2C, local_faults, stats, aborted); });
  c2s.join();
  s2c.join();
  tcp_close(client_fd);
  tcp_close(target_fd);
  stats_ = stats;
}

} // namespace chaoslab
