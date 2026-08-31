#include "test_harness.h"
#include "chaoslab/assertions.h"
using namespace chaoslab;

static AssertionSpec spec(AssertionKind k) { AssertionSpec a; a.kind = k; a.id = AssertionId(1); return a; }

TEST(eval_accepted) { auto a = spec(AssertionKind::ASSERT_ACCEPTED); RunFacts f; f.accepted = true; EXPECT_TRUE(evaluate_assertion(a, f).passed()); f.accepted = false; EXPECT_FALSE(evaluate_assertion(a, f).passed()); }
TEST(eval_rejected) { auto a = spec(AssertionKind::ASSERT_REJECTED); RunFacts f; f.rejected = true; EXPECT_TRUE(evaluate_assertion(a, f).passed()); }
TEST(eval_state) { auto a = spec(AssertionKind::ASSERT_STATE); RunFacts f; f.expected_state = "registered"; f.state = "registered"; EXPECT_TRUE(evaluate_assertion(a, f).passed()); f.state = "dead"; EXPECT_FALSE(evaluate_assertion(a, f).passed()); }
TEST(eval_terminal) { auto a = spec(AssertionKind::ASSERT_TERMINAL); RunFacts f; f.terminal = true; EXPECT_TRUE(evaluate_assertion(a, f).passed()); }
TEST(eval_not_terminal) { auto a = spec(AssertionKind::ASSERT_NOT_TERMINAL); RunFacts f; f.terminal = false; EXPECT_TRUE(evaluate_assertion(a, f).passed()); }
TEST(eval_one_authority) { auto a = spec(AssertionKind::ASSERT_EXACTLY_ONE_AUTHORITY); RunFacts f; f.authority_count = 1; EXPECT_TRUE(evaluate_assertion(a, f).passed()); f.authority_count = 2; EXPECT_FALSE(evaluate_assertion(a, f).passed()); }
TEST(eval_no_stale_mutation) { auto a = spec(AssertionKind::ASSERT_NO_STALE_MUTATION); RunFacts f; f.stale_mutation = false; EXPECT_TRUE(evaluate_assertion(a, f).passed()); f.stale_mutation = true; EXPECT_FALSE(evaluate_assertion(a, f).passed()); }
TEST(eval_no_double_commit) { auto a = spec(AssertionKind::ASSERT_NO_DOUBLE_COMMIT); RunFacts f; f.double_commit = false; EXPECT_TRUE(evaluate_assertion(a, f).passed()); }
TEST(eval_no_leak) { auto a = spec(AssertionKind::ASSERT_NO_LEAK); RunFacts f; f.leak = false; EXPECT_TRUE(evaluate_assertion(a, f).passed()); f.leak = true; EXPECT_FALSE(evaluate_assertion(a, f).passed()); }
TEST(eval_accounting_zero) { auto a = spec(AssertionKind::ASSERT_ACCOUNTING_ZERO); RunFacts f; f.accounting_zero = true; f.accounting = 0; EXPECT_TRUE(evaluate_assertion(a, f).passed()); f.accounting_zero = false; f.accounting = 5; EXPECT_FALSE(evaluate_assertion(a, f).passed()); }
TEST(eval_digest_equal) { auto a = spec(AssertionKind::ASSERT_DIGEST_EQUAL); RunFacts f; f.digest_actual = "aa"; f.digest_expected = "aa"; EXPECT_TRUE(evaluate_assertion(a, f).passed()); }
TEST(eval_digest_different) { auto a = spec(AssertionKind::ASSERT_DIGEST_DIFFERENT); RunFacts f; f.digest_actual = "aa"; f.digest_expected = "bb"; EXPECT_TRUE(evaluate_assertion(a, f).passed()); }
TEST(eval_recovery_complete) { auto a = spec(AssertionKind::ASSERT_RECOVERY_COMPLETE); RunFacts f; f.recovery_complete = true; EXPECT_TRUE(evaluate_assertion(a, f).passed()); }
TEST(eval_process_exit) { auto a = spec(AssertionKind::ASSERT_PROCESS_EXIT); RunFacts f; f.process_exited = true; f.process_exit_code = 7; EXPECT_TRUE(evaluate_assertion(a, f).passed()); }
TEST(eval_process_alive) { auto a = spec(AssertionKind::ASSERT_PROCESS_ALIVE); RunFacts f; f.process_alive = true; EXPECT_TRUE(evaluate_assertion(a, f).passed()); }
TEST(eval_resource_baseline) { auto a = spec(AssertionKind::ASSERT_RESOURCE_BASELINE); RunFacts f; f.resource_baseline_ok = true; f.resource_after = 0; f.resource_baseline = 0; EXPECT_TRUE(evaluate_assertion(a, f).passed()); f.resource_baseline_ok = false; EXPECT_FALSE(evaluate_assertion(a, f).passed()); }

TEST(eval_result_metadata) {
  AssertionSpec a; a.kind = AssertionKind::ASSERT_REJECTED; a.id = AssertionId(42); a.set_param("target", TargetId(9).str());
  RunFacts f; f.rejected = true;
  auto r = evaluate_assertion(a, f, "verifying");
  EXPECT_EQ(r.id.value(), 42u);
  EXPECT_EQ(r.target.value(), 9u);
  EXPECT_EQ(r.phase, "verifying");
  EXPECT_EQ(r.status, AssertionStatus::PASSED);
  EXPECT_FALSE(r.explanation.empty());
}
RUN_ALL
