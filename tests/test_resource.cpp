#include "test_harness.h"
#include "chaoslab/resource.h"
using namespace chaoslab;

TEST(resource_host_pressure_bounded) {
  std::vector<HostAllocation> allocs;
  std::uint64_t total = 0;
  // target 64 MiB with a 32 MiB cap -> total must not exceed cap.
  Status s = apply_host_pressure(64*1024*1024, 32*1024*1024, allocs, total);
  EXPECT_TRUE(s.okay());
  EXPECT_LE(total, 32ull*1024*1024);
  for (auto& a : allocs) a.release();
}

TEST(resource_host_pressure_release) {
  std::vector<HostAllocation> allocs;
  std::uint64_t total = 0;
  apply_host_pressure(16*1024*1024, 0, allocs, total); // 0 cap = unbounded (bounded by target)
  EXPECT_GE(total, 0u);
  EXPECT_TRUE(!allocs.empty());
  for (auto& a : allocs) a.release();
}

TEST(resource_governor_cap) {
  ResourceEnvelope env; env.max_host_allocation_bytes = 100;
  ResourceGovernor g(env);
  std::size_t allocated = 0;
  EXPECT_TRUE(g.try_allocate(50, allocated).okay());
  EXPECT_FALSE(g.try_allocate(60, allocated).okay()); // exceeds cap
  g.account_free(50);
  EXPECT_TRUE(g.try_allocate(40, allocated).okay());
}
RUN_ALL
