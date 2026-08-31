#include "test_harness.h"
#include "chaoslab/replay.h"
using namespace chaoslab;

TEST(replay_compare_same) {
  EvidenceRecorder a, b;
  a.record(EvidenceKind::INJECTION, "p", {{"f","x"}}, "");
  b.record(EvidenceKind::INJECTION, "p", {{"f","x"}}, "");
  ReplayReport r = compare_runs(a.records(), b.records());
  EXPECT_TRUE(r.all_same());
}

TEST(replay_compare_differ) {
  EvidenceRecorder a, b;
  a.record(EvidenceKind::INJECTION, "p", {{"f","x"}}, "");
  b.record(EvidenceKind::INJECTION, "p", {{"f","y"}}, "");
  ReplayReport r = compare_runs(a.records(), b.records());
  EXPECT_FALSE(r.same_state_digest);
}

TEST(replay_evidence_digest) {
  EvidenceRecorder r;
  r.record(EvidenceKind::MISC, "p", {{"k","1"}}, "z");
  Digest256 d = r.digest();
  // Re-evaluate from parsed records.
  std::vector<EvidenceRecord> parsed;
  EXPECT_TRUE(EvidenceRecorder::parse_text(r.serialize_text(), parsed));
  ReplayReport rep = replay_evidence(parsed, d);
  EXPECT_TRUE(rep.same_state_digest);
}
RUN_ALL
