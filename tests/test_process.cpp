#include "test_harness.h"
#include "chaoslab/process.h"
#include <chrono>
#include <thread>
using namespace chaoslab;

TEST(process_exit_code) {
  Process p;
  Status s = Process::launch("cmd.exe", {"/c", "exit", "7"}, "", {}, p);
  EXPECT_TRUE(s.okay());
  EXPECT_TRUE(p.alive() || p.exited()); // could exit very fast
  p.wait(5000);
  EXPECT_TRUE(p.exited());
  EXPECT_EQ(p.exit_code(), 7);
  p.close();
}

TEST(process_kill) {
  Process p;
  // A process that sleeps for a long time; then kill it.
  Status s = Process::launch("cmd.exe", {"/c", "timeout", "/t", "20", "/nobreak", ">nul"}, "", {}, p);
  EXPECT_TRUE(s.okay());
  EXPECT_TRUE(p.alive());
  Status k = p.terminate(false);
  EXPECT_TRUE(k.okay());
  p.wait(3000);
  EXPECT_TRUE(p.exited());
  EXPECT_NE(p.exit_code(), 0); // terminated
  p.close();
}
RUN_ALL
