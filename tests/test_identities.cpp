#include "test_harness.h"
#include "chaoslab/identity.h"
using namespace chaoslab;

TEST(identity_roundtrip) {
  CampaignId c(0xdeadbeefdeadbeefull);
  std::string s = c.to_string();
  CampaignId c2;
  EXPECT_TRUE(CampaignId::parse(s, c2));
  EXPECT_EQ(c.value(), c2.value());
  EXPECT_TRUE(c == c2);
  // str() form round-trips too.
  CampaignId c3;
  EXPECT_TRUE(CampaignId::parse(c.str(), c3));
  EXPECT_EQ(c.value(), c3.value());
  EXPECT_EQ(c.kind(), IdKind::Campaign);
}

TEST(identity_kind_reject) {
  CampaignId c(5);
  WorkerId w(7);
  CampaignId out;
  EXPECT_FALSE(WorkerId::parse(c.str(), w)); // wrong kind prefix
}
RUN_ALL
