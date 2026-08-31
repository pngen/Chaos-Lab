#include "test_harness.h"
#include "chaoslab/evidence.h"
using namespace chaoslab;

TEST(evidence_roundtrip) {
  EvidenceRecorder r;
  r.record(EvidenceKind::PROCESS_LAUNCH, "setup", {{"target", "coordinator"}, {"port", "12345"}}, "args here");
  r.record(EvidenceKind::INJECTION, "injecting", {{"fault", "worker_death"}}, "");
  std::string txt = r.serialize_text();
  std::vector<EvidenceRecord> out;
  EXPECT_TRUE(EvidenceRecorder::parse_text(txt, out));
  EXPECT_EQ(out.size(), 2u);
  EXPECT_EQ(out[0].kind, EvidenceKind::PROCESS_LAUNCH);
  EXPECT_EQ(out[0].field_or("target", ""), "coordinator");
}

TEST(evidence_digest_deterministic) {
  EvidenceRecorder a, b;
  a.record(EvidenceKind::MISC, "p", {{"k","1"}}, "x");
  b.record(EvidenceKind::MISC, "p", {{"k","1"}}, "x");
  EXPECT_EQ(a.digest().hex(), b.digest().hex());
  a.record(EvidenceKind::MISC, "p", {{"k","2"}}, "y");
  EXPECT_NE(a.digest().hex(), b.digest().hex());
}

TEST(evidence_json_valid) {
  EvidenceRecorder r;
  r.record(EvidenceKind::MISC, "phase", {{"a","value"},{"d","e"}}, "p");
  std::string j = r.serialize_json();
  EXPECT_FALSE(j.empty());
  EXPECT_TRUE(j.find("\"a\":\"value\"") != std::string::npos);
  EXPECT_TRUE(j.find("\"phase\":\"phase\"") != std::string::npos);
}
RUN_ALL
