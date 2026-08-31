#pragma once
// Minimal TCP socket helpers (Winsock) for the transport interposer.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include "chaoslab/result.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace chaoslab {

/// A TCP endpoint (host + port).
struct TcpEndpoint {
  std::string host;
  std::uint16_t port{0};
};

struct TcpSocket {
  int fd = -1;
  bool valid() const noexcept { return fd >= 0; }
};

/// Global socket subsystem init / teardown (WSAStartup/WSACleanup).
Status tcp_init();
void tcp_shutdown() noexcept;

/// Create a listening socket bound to the given port (0 = ephemeral). On success
/// writes the bound port to out_port.
Status tcp_listen(std::uint16_t port, int& out_fd, std::uint16_t& out_port);

/// Accept a single connection on server_fd.
Status tcp_accept(int server_fd, int& out_fd, TcpEndpoint& peer);

/// Connect to a host/port.
Status tcp_connect(const std::string& host, std::uint16_t port, int& out_fd);

/// Send all bytes. Returns bytes sent on success (may be short).
Status tcp_send_all(int fd, const void* data, std::size_t len, std::size_t& sent);

/// Receive exactly n bytes; returns received bytes (0 = peer closed).
Status tcp_recv_some(int fd, void* buf, std::size_t cap, std::size_t& received);

/// Set receive timeout in ms.
Status tcp_set_recv_timeout(int fd, int timeout_ms);

void tcp_close(int& fd) noexcept;

} // namespace chaoslab
