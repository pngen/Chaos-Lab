#include "chaoslab/tcp.h"

#include "chaoslab/windows_error.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <cstring>

namespace chaoslab {

Status tcp_init() {
#ifdef _WIN32
  WSADATA d;
  int r = WSAStartup(MAKEWORD(2, 2), &d);
  if (r != 0) return Status::error(StatusCode::io_error, "WSAStartup failed: " + std::to_string(r));
#endif
  return Status::ok();
}

void tcp_shutdown() noexcept {
#ifdef _WIN32
  WSACleanup();
#endif
}

namespace {
std::string sockerr() {
#ifdef _WIN32
  return last_error_message(static_cast<unsigned long>(WSAGetLastError()));
#else
  return std::strerror(errno);
#endif
}
} // namespace

Status tcp_listen(std::uint16_t port, int& out_fd, std::uint16_t& out_port) {
#ifdef _WIN32
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return Status::error(StatusCode::io_error, "socket(): " + sockerr());
  BOOL reuse = TRUE;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof reuse);
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_addr.s_addr = htonl(INADDR_ANY);
  a.sin_port = htons(port);
  if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == SOCKET_ERROR) {
    closesocket(s);
    return Status::error(StatusCode::io_error, "bind(): " + sockerr());
  }
  if (listen(s, 16) == SOCKET_ERROR) {
    closesocket(s);
    return Status::error(StatusCode::io_error, "listen(): " + sockerr());
  }
  sockaddr_in bound;
  int len = sizeof bound;
  getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
  out_fd = static_cast<int>(s);
  out_port = ntohs(bound.sin_port);
  return Status::ok();
#else
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return Status::error(StatusCode::io_error, "socket()");
  int yes = 1;
  setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
  sockaddr_in a; std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_addr.s_addr = htonl(INADDR_ANY); a.sin_port = htons(port);
  if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) { close(s); return Status::error(StatusCode::io_error, "bind()"); }
  if (listen(s, 16) < 0) { close(s); return Status::error(StatusCode::io_error, "listen()"); }
  sockaddr_in bound; socklen_t len = sizeof bound; getsockname(s, reinterpret_cast<sockaddr*>(&bound), &len);
  out_fd = s; out_port = ntohs(bound.sin_port);
  return Status::ok();
#endif
}

Status tcp_accept(int server_fd, int& out_fd, TcpEndpoint& peer) {
#ifdef _WIN32
  sockaddr_in a; int len = sizeof a;
  SOCKET c = accept(static_cast<SOCKET>(server_fd), reinterpret_cast<sockaddr*>(&a), &len);
  if (c == INVALID_SOCKET) return Status::error(StatusCode::io_error, "accept(): " + sockerr());
  out_fd = static_cast<int>(c);
  char host[64]; inet_ntop(AF_INET, &a.sin_addr, host, sizeof host);
  peer.host = host; peer.port = ntohs(a.sin_port);
  return Status::ok();
#else
  sockaddr_in a; socklen_t len = sizeof a;
  int c = accept(server_fd, reinterpret_cast<sockaddr*>(&a), &len);
  if (c < 0) return Status::error(StatusCode::io_error, "accept()");
  out_fd = c;
  char host[64]; inet_ntop(AF_INET, &a.sin_addr, host, sizeof host);
  peer.host = host; peer.port = ntohs(a.sin_port);
  return Status::ok();
#endif
}

Status tcp_connect(const std::string& host, std::uint16_t port, int& out_fd) {
#ifdef _WIN32
  SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (s == INVALID_SOCKET) return Status::error(StatusCode::io_error, "socket(): " + sockerr());
  sockaddr_in a;
  std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET;
  a.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) {
    // try resolve
    addrinfo hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &res) == 0 && res) {
      std::memcpy(&a, res->ai_addr, sizeof a);
      freeaddrinfo(res);
    } else { closesocket(s); return Status::error(StatusCode::io_error, "resolve failed: " + host); }
  }
  if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) == SOCKET_ERROR) {
    closesocket(s);
    return Status::error(StatusCode::io_error, "connect(): " + sockerr());
  }
  out_fd = static_cast<int>(s);
  return Status::ok();
#else
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return Status::error(StatusCode::io_error, "socket()");
  sockaddr_in a; std::memset(&a, 0, sizeof a);
  a.sin_family = AF_INET; a.sin_port = htons(port);
  if (inet_pton(AF_INET, host.c_str(), &a.sin_addr) != 1) { close(s); return Status::error(StatusCode::io_error, "resolve"); }
  if (connect(s, reinterpret_cast<sockaddr*>(&a), sizeof a) < 0) { close(s); return Status::error(StatusCode::io_error, "connect()"); }
  out_fd = s;
  return Status::ok();
#endif
}

Status tcp_send_all(int fd, const void* data, std::size_t len, std::size_t& sent) {
  sent = 0;
  const char* p = static_cast<const char*>(data);
  while (sent < len) {
#ifdef _WIN32
    int n = send(static_cast<SOCKET>(fd), p + sent, static_cast<int>(len - sent), 0);
#else
    ssize_t n = send(fd, p + sent, len - sent, 0);
#endif
    if (n <= 0) return Status::error(StatusCode::io_error, "send()");
    sent += static_cast<std::size_t>(n);
  }
  return Status::ok();
}

Status tcp_recv_some(int fd, void* buf, std::size_t cap, std::size_t& received) {
  received = 0;
#ifdef _WIN32
  int n = recv(static_cast<SOCKET>(fd), static_cast<char*>(buf), static_cast<int>(cap), 0);
#else
  ssize_t n = recv(fd, buf, cap, 0);
#endif
  if (n == 0) return Status::ok(); // peer closed
  if (n < 0) return Status::error(StatusCode::io_error, "recv()");
  received = static_cast<std::size_t>(n);
  return Status::ok();
}

Status tcp_set_recv_timeout(int fd, int timeout_ms) {
#ifdef _WIN32
  DWORD t = static_cast<DWORD>(timeout_ms);
  if (setsockopt(static_cast<SOCKET>(fd), SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&t), sizeof t) != 0)
    return Status::error(StatusCode::io_error, "setsockopt(SO_RCVTIMEO)");
#else
  timeval t; t.tv_sec = timeout_ms / 1000; t.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof t);
#endif
  return Status::ok();
}

void tcp_close(int& fd) noexcept {
#ifdef _WIN32
  if (fd >= 0) closesocket(static_cast<SOCKET>(fd));
#else
  if (fd >= 0) close(fd);
#endif
  fd = -1;
}

} // namespace chaoslab
