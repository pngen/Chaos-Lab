#include "test_harness.h"
#include "chaoslab/authority.h"
using namespace chaoslab;

TEST(authority_roundtrip) {
  AuthorityEnvelope a;
  a.epoch = CoordinatorEpoch(1); a.worker = WorkerId(2); a.boot = WorkerBootId(3);
  a.attempt = AttemptId(4); a.attempt_gen = AttemptGeneration(5);
  a.dispatch = DispatchId(6); a.target_gen = TargetGeneration(7);
  std::string t = a.to_text();
  AuthorityEnvelope a2;
  Status s = AuthorityEnvelope::parse(t, a2);
  EXPECT_TRUE(s.okay());
  EXPECT_TRUE(a == a2);
}

TEST(authority_stale) {
  AuthorityEnvelope cur;
  cur.epoch = CoordinatorEpoch(10); cur.boot = WorkerBootId(101);
  cur.attempt = AttemptId(100); cur.attempt_gen = AttemptGeneration(100);
  cur.dispatch = DispatchId(200); cur.target_gen = TargetGeneration(50);
  // A stale boot is older.
  AuthorityEnvelope stale = cur; stale.boot = WorkerBootId(100);
  EXPECT_TRUE(authority_is_stale(cur, stale));
  EXPECT_TRUE(authority_matches(cur, cur));
  // stale generation
  AuthorityEnvelope sg = cur; sg.target_gen = TargetGeneration(49);
  EXPECT_TRUE(authority_is_stale(cur, sg));
  EXPECT_EQ(stale_dimension(cur, sg), "target_gen");
}

TEST(authority_combos) {
  AuthorityEnvelope cur;
  cur.epoch = CoordinatorEpoch(50); cur.boot = WorkerBootId(1000);
  cur.attempt = AttemptId(100); cur.attempt_gen = AttemptGeneration(100);
  cur.dispatch = DispatchId(200); cur.target_gen = TargetGeneration(500);
  EXPECT_TRUE(authority_is_stale(cur, stale_combination(0, cur))); // stale boot
  EXPECT_TRUE(authority_is_stale(cur, stale_combination(1, cur))); // stale attempt
  EXPECT_TRUE(authority_is_stale(cur, stale_combination(2, cur))); // stale dispatch
  EXPECT_EQ(stale_combination(4, cur), cur); // duplicate is identical
  EXPECT_FALSE(authority_matches(cur, stale_combination(5, cur))); // conflicting epoch differs
}
RUN_ALL
