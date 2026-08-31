#include "test_harness.h"
#include "chaoslab/safety.h"
using namespace chaoslab;

TEST(safety_envelope) {
  ResourceEnvelope env;
  env.max_child_processes = 2;
  SafetyEnvelope se(env);
  EXPECT_TRUE(se.acquire_child().okay());
  EXPECT_TRUE(se.acquire_child().okay());
  EXPECT_FALSE(se.acquire_child().okay()); // cap reached
  se.release_child();
  EXPECT_TRUE(se.acquire_child().okay());
}

TEST(safety_targets) {
  ResourceEnvelope env;
  SafetyEnvelope se(env);
  TargetId t((1ULL<<63)|7);
  se.add_owned_target(t);
  EXPECT_TRUE(se.allow_target(t).okay());
  TargetId other(5);
  EXPECT_FALSE(se.allow_target(other).okay());
}

TEST(safety_exe) {
  ResourceEnvelope env;
  env.allowed_executables = {"target_worker"}; 
  SafetyEnvelope se(env);
  EXPECT_TRUE(se.allow_executable("target_worker").okay());
  EXPECT_FALSE(se.allow_executable("evil.exe").okay());
}
RUN_ALL
