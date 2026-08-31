#include "test_harness.h"
#include "chaoslab/fuzz.h"
using namespace chaoslab;

TEST(fuzz_deterministic_seed) {
  CampaignFuzzer f1(42), f2(42);
  ChaosCampaign a = f1.generate();
  ChaosCampaign b = f2.generate();
  EXPECT_EQ(a.seed, b.seed);
  EXPECT_EQ(a.fault_schedule.size(), b.fault_schedule.size());
  for (std::size_t i = 0; i < a.fault_schedule.size(); ++i) {
    EXPECT_EQ(a.fault_schedule[i].category, b.fault_schedule[i].category);
    EXPECT_EQ(a.fault_schedule[i].target.value(), b.fault_schedule[i].target.value());
  }
}

TEST(fuzz_differs_by_seed) {
  CampaignFuzzer f1(1), f2(2);
  ChaosCampaign a = f1.generate();
  ChaosCampaign b = f2.generate();
  bool differs = (a.fault_schedule.size() != b.fault_schedule.size());
  if (!differs) {
    for (std::size_t i = 0; i < a.fault_schedule.size() && !differs; ++i)
      if (a.fault_schedule[i].category != b.fault_schedule[i].category) differs = true;
  }
  EXPECT_TRUE(differs);
}

TEST(fuzz_reduce_preserves_failure) {
  CampaignFuzzer f(77);
  ChaosCampaign c = f.generate();
  ChaosCampaign reduced = reduce_campaign(c, [](const ChaosCampaign&) { return true; });
  EXPECT_LE(reduced.fault_schedule.size(), c.fault_schedule.size());
  // An always-failing predicate lets the reducer remove every fault.
  EXPECT_EQ(reduced.fault_schedule.size(), 0u);
}
RUN_ALL
