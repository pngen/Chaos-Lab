#pragma once
// Dependency-free deterministic test harness for Chaos Lab.
// No timeouts, no watchdogs, no external runner.
// Copyright 2026 Summon Software Labs. SPDX-License-Identifier: Apache-2.0

#include <cstdio>
#include <string>
#include <vector>

namespace testharness {
struct Test { std::string name; void (*fn)(); };
inline std::vector<Test>& registry() { static std::vector<Test> r; return r; }
inline int& failures() { static int f = 0; return f; }
inline int& checks() { static int c = 0; return c; }
inline void expect(bool cond, const char* file, int line, const char* expr) {
  ++checks();
  if (!cond) { ++failures(); std::printf("FAILED %s:%d: %s\n", file, line, expr); }
}
}

#define TEST(name)   static void name();   static const int reg_##name = (testharness::registry().push_back({#name, name}), 0);   static void name()
#define EXPECT(cond) testharness::expect((cond), __FILE__, __LINE__, #cond)
#define EXPECT_TRUE(cond) testharness::expect((cond), __FILE__, __LINE__, #cond)
#define EXPECT_FALSE(cond) testharness::expect(!(cond), __FILE__, __LINE__, #cond)
#define EXPECT_EQ(a, b) do { auto _a = (a); auto _b = (b); testharness::expect(_a == _b, __FILE__, __LINE__, #a " == " #b); } while (0)
#define EXPECT_NE(a, b) do { auto _a = (a); auto _b = (b); testharness::expect(_a != _b, __FILE__, __LINE__, #a " != " #b); } while (0)
#define EXPECT_GE(a, b) do { auto _a = (a); auto _b = (b); testharness::expect(_a >= _b, __FILE__, __LINE__, #a " >= " #b); } while (0)
#define EXPECT_LE(a, b) do { auto _a = (a); auto _b = (b); testharness::expect(_a <= _b, __FILE__, __LINE__, #a " <= " #b); } while (0)
#define EXPECT_LT(a, b) do { auto _a = (a); auto _b = (b); testharness::expect(_a < _b, __FILE__, __LINE__, #a " < " #b); } while (0)
#define EXPECT_GT(a, b) do { auto _a = (a); auto _b = (b); testharness::expect(_a > _b, __FILE__, __LINE__, #a " > " #b); } while (0)
#define RUN_ALL   int main() {     for (auto& t : testharness::registry()) { std::printf("RUN %s\n", t.name.c_str()); t.fn(); }     std::printf("checks=%d failures=%d\n", testharness::checks(), testharness::failures());     return testharness::failures() ? 1 : 0;   }
