#include "test_harness.h"
#include "chaoslab/assertions.h"
using namespace chaoslab;

TEST(assertion_kind_names) {
  EXPECT_EQ(std::string(assertion_kind_name(AssertionKind::ASSERT_REJECTED)), "rejected");
  AssertionKind k;
  EXPECT_TRUE(parse_assertion_kind("rejected", k));
  EXPECT_FALSE(parse_assertion_kind("bogus", k));
}

TEST(assertion_result) {
  AssertionResult r;
  r.status = AssertionStatus::PASSED;
  EXPECT_TRUE(r.passed());
  r.status = AssertionStatus::FAILED;
  EXPECT_FALSE(r.passed());
}

TEST(assertion_params) {
  AssertionSpec a;
  a.set_param("state", "registered");
  EXPECT_EQ(a.params["state"], "registered");
}
RUN_ALL
