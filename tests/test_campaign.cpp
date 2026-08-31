#include "test_harness.h"
#include "chaoslab/campaign.h"
using namespace chaoslab;

TEST(campaign_phases) {
  EXPECT_FALSE(can_transition(CampaignPhase::COMPLETE, CampaignPhase::ARMED)); // no re-entry
  EXPECT_TRUE(can_transition(CampaignPhase::SETUP, CampaignPhase::BASELINE));
  EXPECT_TRUE(can_transition(CampaignPhase::BASELINE, CampaignPhase::ARMED));
  EXPECT_TRUE(can_transition(CampaignPhase::ARMED, CampaignPhase::INJECTING));
  EXPECT_TRUE(can_transition(CampaignPhase::INJECTING, CampaignPhase::OBSERVING));
  EXPECT_FALSE(can_transition(CampaignPhase::OBSERVING, CampaignPhase::CLEANUP)); // must pass through RECOVERING/VERIFYING
  EXPECT_TRUE(is_terminal(CampaignPhase::FAILED));
  EXPECT_TRUE(is_terminal(CampaignPhase::ABORTED));
}

TEST(campaign_validate) {
  ChaosCampaign c;
  c.seed = 123;
  c.envelope.max_child_processes = 2;
  std::string err;
  EXPECT_TRUE(c.validate(err));
  c.seed = 0;
  EXPECT_FALSE(c.validate(err));
}
RUN_ALL
