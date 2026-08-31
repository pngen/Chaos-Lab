#include "test_harness.h"
#include "chaoslab/digest.h"
using namespace chaoslab;

TEST(digest_known_vector) {
  // SHA-256("abc")
  Digest256 d = Digest256::sha256("abc");
  EXPECT_EQ(d.hex(), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(digest_roundtrip) {
  Digest256 d = Digest256::sha256("hello");
  std::string h = d.hex();
  Digest256 d2;
  EXPECT_TRUE(Digest256::parse(h, d2));
  EXPECT_TRUE(d == d2);
  EXPECT_FALSE(Digest256::parse("nothex", d2));
}
RUN_ALL
