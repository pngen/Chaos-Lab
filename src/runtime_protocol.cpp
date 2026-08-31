#include "chaoslab/runtime_protocol.h"

#include "chaoslab/tcp.h"

#include <cstring>

namespace chaoslab {
namespace proto {

Status send_frame(int fd, std::uint32_t type, const std::string& payload) {
  Frame f;
  f.type = type;
  f.payload.assign(payload.begin(), payload.end());
  std::vector<std::uint8_t> bytes = f.encode();
  std::size_t sent = 0;
  return tcp_send_all(fd, bytes.data(), bytes.size(), sent);
}

Status recv_frame(int fd, std::uint32_t& type, std::string& payload) {
  // Read the 12-byte header, then the body.
  std::uint8_t hdr[Frame::HEADER];
  std::size_t got = 0;
  while (got < Frame::HEADER) {
    std::size_t recvd = 0;
    Status s = tcp_recv_some(fd, hdr + got, Frame::HEADER - got, recvd);
    if (s.failed()) return s;
    if (recvd == 0) return Status::error(StatusCode::io_error, "connection closed while reading header");
    got += recvd;
  }
  std::uint32_t len = 0;
  if (!Frame::decode_header(hdr, Frame::HEADER, type, len))
    return Status::error(StatusCode::protocol_error, "bad frame header");
  if (len > (64u << 20))
    return Status::error(StatusCode::protocol_error, "frame too large");
  if (len > 0) {
    std::vector<std::uint8_t> body(len);
    std::size_t b = 0;
    while (b < body.size()) {
      std::size_t recvd = 0;
      Status s = tcp_recv_some(fd, body.data() + b, body.size() - b, recvd);
      if (s.failed()) return s;
      if (recvd == 0) return Status::error(StatusCode::io_error, "connection closed while reading body");
      b += recvd;
    }
    payload.assign(body.begin(), body.end());
  } else {
    payload.clear();
  }
  return Status::ok();
}

Status pick_ephemeral_port(std::uint16_t& port) {
  int fd = -1;
  std::uint16_t bound = 0;
  Status s = tcp_listen(0, fd, bound);
  if (s.failed()) return s;
  tcp_close(fd);
  port = bound;
  return Status::ok();
}

void put_u64(std::string& out, std::uint64_t v) {
  for (int i = 7; i >= 0; --i) out.push_back(static_cast<char>((v >> (8 * i)) & 0xff));
}

bool get_u64(const std::string& in, std::size_t off, std::uint64_t& out) {
  if (off + 8 > in.size()) return false;
  std::uint64_t v = 0;
  for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<std::uint8_t>(in[off + static_cast<std::size_t>(i)]);
  out = v;
  return true;
}

} // namespace proto
} // namespace chaoslab
