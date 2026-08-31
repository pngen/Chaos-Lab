#include "test_harness.h"
#include "chaoslab/persistence.h"
#include <cstdio>
#include <fstream>
using namespace chaoslab;

TEST(persistence_digest) {
  std::string p = "test_digest.bin";
  { std::ofstream f(p, std::ios::binary); f << "hello chaos"; }
  Digest256 d1, d2;
  EXPECT_TRUE(file_digest(p, d1).okay());
  EXPECT_TRUE(file_digest(p, d2).okay());
  EXPECT_EQ(d1.hex(), d2.hex());
  std::remove(p.c_str());
}

TEST(persistence_truncate) {
  std::string p = "test_trunc.bin";
  {
    std::ofstream f(p, std::ios::binary);
    std::uint8_t data[100]; for (int i=0;i<100;++i) data[i]=static_cast<std::uint8_t>(i);
    f.write(reinterpret_cast<char*>(data), 100);
  }
  PersistenceMutation m; m.kind = MutationKind::TRUNCATE; m.offset = 40;
  EXPECT_TRUE(apply_mutation(p, m).okay());
  Digest256 d; EXPECT_TRUE(file_digest(p, d).okay());
  std::ifstream f(p, std::ios::binary | std::ios::ate);
  auto sz = f.tellg();
  EXPECT_EQ(static_cast<long>(sz), 40L);
  std::remove(p.c_str());
}

TEST(persistence_partial_temp) {
  std::string p = "test_partial.tmp";
  std::uint8_t bytes[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
  EXPECT_TRUE(write_partial_temp(p, bytes, 8).okay());
  EXPECT_TRUE(file_exists(p));
  std::remove(p.c_str());
}

TEST(persistence_magic_corrupt) {
  std::string p = "test_magic.bin";
  { std::ofstream f(p, std::ios::binary); f << "MAGICDATA"; }
  PersistenceMutation m; m.kind = MutationKind::OVERWRITE_MAGIC;
  EXPECT_TRUE(apply_mutation(p, m).okay());
  std::ifstream f(p, std::ios::binary); 
  char c; f.read(&c,1);
  EXPECT_EQ(static_cast<unsigned char>(c), 0xde);
  std::remove(p.c_str());
}
RUN_ALL
