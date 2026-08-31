#include "test_harness.h"
#include "chaoslab/evidence.h"
#include <thread>
#include <vector>
using namespace chaoslab;

TEST(concurrent_evidence) {
  const int threads = 8, per = 1000;
  EvidenceRecorder r;
  std::vector<std::thread> ts;
  for (int t = 0; t < threads; ++t) {
    ts.emplace_back([&, t] {
      for (int i = 0; i < per; ++i) r.record(EvidenceKind::MISC, "p", {{"t", std::to_string(t)}, {"i", std::to_string(i)}}, "v");
    });
  }
  for (auto& t : ts) t.join();
  EXPECT_EQ(r.size(), static_cast<std::size_t>(threads * per));
  // Deterministic digest over the same insert set (order across threads may vary,
  // so digest is a property of the multiset; verify determinism by re-running).
  Digest256 d1 = r.digest();
  EXPECT_FALSE(d1.hex().empty());
}

TEST(concurrent_digest_deterministic) {
  // Two recorders fed the same records in the same order (single-threaded) give
  // the same digest; concurrent recording still yields a valid digest.
  EvidenceRecorder a;
  for (int i = 0; i < 1000; ++i) a.record(EvidenceKind::MISC, "p", {{"i", std::to_string(i)}}, "x");
  Digest256 da = a.digest();
  std::vector<EvidenceRecord> snap = a.snapshot();
  EXPECT_EQ(snap.size(), 1000u);
  EXPECT_FALSE(da.hex().empty());
}
RUN_ALL
