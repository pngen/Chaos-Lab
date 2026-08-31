#include "test_harness.h"
#include "chaoslab/process.h"
#include <cstdlib>
#include <string>
#include <chrono>
#include <thread>
using namespace chaoslab;

TEST(multiprocess_closure_proof) {
  char* bin = nullptr;
  std::size_t blen = 0;
  if (_dupenv_s(&bin, &blen, "CHAOSLAB_BIN") != 0 || !bin) bin = nullptr;
  std::string chaos = bin ? std::string(bin) : "chaos.exe";
  if (bin) std::free(bin);
  Process p;
  Status s = Process::launch(chaos, {"multiprocess", "--count", "2"}, "", {}, p);
  EXPECT_TRUE(s.okay());
  p.wait(60000);
  std::string out = p.read_stdout();
  out += p.read_stderr();
  int rc = p.exit_code();
  std::printf("MP rc=%d out=%s\n", rc, out.c_str());
  EXPECT_EQ(rc, 0);
  EXPECT_TRUE(out.find("PASS") != std::string::npos);
  p.close();
}
RUN_ALL
