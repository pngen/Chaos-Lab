#include "test_harness.h"
#include "chaoslab/runtime_protocol.h"
#include "chaoslab/transport.h"
#include "chaoslab/tcp.h"
#include <atomic>
#include <thread>
#include <vector>
using namespace chaoslab;

TEST(frame_codec_roundtrip) {
  Frame f;
  f.type = 0x1234;
  f.payload = {1,2,3,4,5};
  std::vector<std::uint8_t> b = f.encode();
  Frame out; std::size_t consumed = 0;
  EXPECT_TRUE(Frame::decode(b.data(), b.size(), out, consumed));
  EXPECT_EQ(out.type, 0x1234u);
  EXPECT_EQ(out.payload.size(), 5u);
  EXPECT_EQ(out.payload[0], 1);
  EXPECT_EQ(consumed, b.size());
}

TEST(transport_drop_frame) {
  EXPECT_TRUE(tcp_init().okay());
  // Echo server upstream.
  int srv = -1; std::uint16_t srv_port = 0;
  EXPECT_TRUE(tcp_listen(0, srv, srv_port).okay());
  std::atomic<int> received{0};
  std::atomic<bool> stop{false};
  std::thread server([&] {
    int cfd = -1; TcpEndpoint peer;
    if (tcp_accept(srv, cfd, peer).failed()) return;
    while (!stop.load()) {
      std::uint32_t type = 0; std::string payload;
      Status s = proto::recv_frame(cfd, type, payload);
      if (s.failed()) break;
      ++received;
      proto::send_frame(cfd, type, payload); // echo
    }
    tcp_close(cfd);
  });

  // Proxy in between.
  FaultyTransport proxy;
  EXPECT_TRUE(proxy.start(0, "127.0.0.1", srv_port).okay());
  std::uint16_t proxy_port = proxy.listen_port();
  TransportFault drop;
  drop.kind = TransportFaultKind::TF_DROP;
  drop.direction = Direction::C2S;
  drop.at_sequence = 2; // drop the 2nd client->server frame
  proxy.enqueue_fault(drop);

  // Client connects through the proxy and sends 3 frames.
  int cfd = -1;
  EXPECT_TRUE(tcp_connect("127.0.0.1", proxy_port, cfd).okay());
  for (int i = 0; i < 3; ++i) proto::send_frame(cfd, 0x0100 + static_cast<std::uint32_t>(i), "f" + std::to_string(i));
  // Let the proxy/echo settle.
  std::this_thread::sleep_for(std::chrono::milliseconds(300));

  EXPECT_EQ(received.load(), 2); // 2nd frame dropped
  EXPECT_EQ(proxy.stats().dropped, 1ull);
  EXPECT_EQ(proxy.stats().faults_applied, 1ull);
  EXPECT_GE(proxy.stats().frames_forwarded, 2ull);

  tcp_close(cfd);
  proxy.stop();
  stop.store(true);
  if (server.joinable()) server.join();
  tcp_close(srv);
  tcp_shutdown();
}
RUN_ALL
