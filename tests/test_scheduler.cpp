#include "test_harness.h"
#include "chaoslab/scheduler.h"
#include "chaoslab/scenario.h"
using namespace chaoslab;

TEST(scheduler_deterministic) {
  ScenarioBuilder b1(0x1234);
  b1.target_worker("w", "target_worker");
  b1.kill_process(TargetId((1ULL<<63)|1));
  b1.inject_fault(FaultCategory::FRAME_CORRUPTION, TargetId((1ULL<<63)|1));
  Scenario s1 = b1.build();

  ScenarioBuilder b2(0x1234);
  b2.target_worker("w", "target_worker");
  b2.kill_process(TargetId((1ULL<<63)|1));
  b2.inject_fault(FaultCategory::FRAME_CORRUPTION, TargetId((1ULL<<63)|1));
  Scenario s2 = b2.build();

  EXPECT_EQ(s1.campaign.fault_schedule.size(), s2.campaign.fault_schedule.size());
  EXPECT_EQ(s1.campaign.fault_schedule[0].category, s2.campaign.fault_schedule[0].category);

  FaultScheduler sc1(s1.campaign.seed), sc2(s2.campaign.seed);
  sc1.plan(s1.campaign); sc2.plan(s2.campaign);
  EXPECT_EQ(sc1.schedule().size(), sc2.schedule().size());
  for (std::size_t i = 0; i < sc1.schedule().size(); ++i)
    EXPECT_EQ(sc1.schedule()[i].id.value(), sc2.schedule()[i].id.value());
}

TEST(scheduler_all_fired) {
  ScenarioBuilder b(0x77);
  b.target_worker("w", "target_worker");
  b.kill_process(TargetId((1ULL<<63)|1));
  Scenario s = b.build();
  FaultScheduler sc(0x77);
  sc.plan(s.campaign);
  EXPECT_FALSE(sc.all_fired());
  // observe events to fire
  for (auto& si : sc.schedule()) { (void)si; }
  sc.observe_event("completion_boundary");
  EXPECT_TRUE(sc.all_fired());
}
RUN_ALL
