#include "test_harness.h"
#include "chaoslab/resource.h"
using namespace chaoslab;

TEST(resource_baseline_clean) {
  ResourceBaselineTracker t;
  ResourceCounts c;
  t.observe(c); // baseline
  c.child_processes = 3; t.observe(c); // peak
  c.child_processes = 0; t.observe(c); // cleaned
  EXPECT_TRUE(t.captured());
  EXPECT_FALSE(t.leak());
  EXPECT_EQ(t.baseline().peak.child_processes, 3u);
}

TEST(resource_baseline_leak) {
  ResourceBaselineTracker t;
  ResourceCounts c;
  t.observe(c);
  c.open_sockets = 2; t.observe(c);
  // after still has a socket => leak
  t.observe(c);
  EXPECT_TRUE(t.leak());
}

TEST(resource_baseline_report) {
  ResourceBaselineTracker t;
  ResourceCounts c;
  t.observe(c);
  c.host_bytes = 100; t.observe(c);
  c.host_bytes = 0; t.observe(c);
  std::string s = t.baseline().delta_report();
  EXPECT_TRUE(s.find("host_bytes") != std::string::npos);
  EXPECT_TRUE(s.find("peak=100") != std::string::npos);
}

TEST(resource_baseline_device) {
  ResourceBaselineTracker t;
  ResourceCounts c;
  t.observe(c);
  c.device_bytes = 512; t.observe(c);
  c.device_bytes = 0; t.observe(c);
  EXPECT_FALSE(t.leak());
}
RUN_ALL
