// Fit test harness.
//
// A minimal test framework: register tests with TEST(name) { ... }, run a
// single named test by passing its name as argv[1] to the resulting binary.
// Assertions throw on failure (caught by the runner); a test that just
// returns passed. REQUIRE_GATE(cond, "reason") is for a negative/end-to-end
// assertion that only means something once an earlier positive case works:
// it is gated behind that case, and reports SKIP (exit code 77) rather than
// a false PASS or a confusing FAIL.
#pragma once
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

struct TestFailure {
  std::string message;
};
struct TestSkip {
  std::string reason;
};

inline std::map<std::string, std::function<void()>> &test_registry() {
  static std::map<std::string, std::function<void()>> reg;
  return reg;
}

struct TestRegistrar {
  TestRegistrar(const std::string &name, std::function<void()> fn) {
    test_registry()[name] = std::move(fn);
  }
};

#define TEST(name)                                                             \
  static void test_fn_##name();                                                \
  static TestRegistrar registrar_##name(#name, test_fn_##name);                \
  static void test_fn_##name()

#define REQUIRE(cond)                                                          \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::ostringstream _oss;                                                 \
      _oss << "REQUIRE failed at " << __FILE__ << ":" << __LINE__ << ": "      \
           << #cond;                                                           \
      throw TestFailure{_oss.str()};                                           \
    }                                                                          \
  } while (0)

#define REQUIRE_EQ(a, b)                                                       \
  do {                                                                         \
    auto _va = (a);                                                            \
    auto _vb = (b);                                                            \
    if (!(_va == _vb)) {                                                       \
      std::ostringstream _oss;                                                 \
      _oss << "REQUIRE_EQ failed at " << __FILE__ << ":" << __LINE__ << ": "   \
           << #a << " != " << #b;                                              \
      throw TestFailure{_oss.str()};                                           \
    }                                                                          \
  } while (0)

// Passes only if `expr` throws std::exception (any subtype).
#define REQUIRE_THROWS(expr)                                                   \
  do {                                                                         \
    bool _threw = false;                                                       \
    try {                                                                      \
      (void)(expr);                                                            \
    } catch (const std::exception &) {                                         \
      _threw = true;                                                           \
    }                                                                          \
    if (!_threw)                                                               \
      throw TestFailure{std::string("expected exception at ") + __FILE__ +     \
                        ":" + std::to_string(__LINE__) + ": " #expr};          \
  } while (0)

// Gate a test behind a precondition (e.g. "the positive case already
// passes"). Skips loudly, so a negative check never passes trivially against
// an implementation that rejects everything.
#define REQUIRE_GATE(cond, reason)                                             \
  do {                                                                         \
    if (!(cond))                                                               \
      throw TestSkip{reason};                                                  \
  } while (0)

// A 15s wall-clock budget per test process invocation, so a stub that
// deadlocks or infinite-loops fails loudly instead of hanging `ctest`.
inline void install_timeout(unsigned seconds = 15) {
  signal(SIGALRM, [](int) {
    fprintf(stderr, "TIMEOUT: test exceeded wall-clock budget\n");
    _exit(124);
  });
  alarm(seconds);
}

inline int run_named_test(const std::string &name) {
  install_timeout();
  auto it = test_registry().find(name);
  if (it == test_registry().end()) {
    fprintf(stderr, "no such test: %s\n", name.c_str());
    return 1;
  }
  try {
    it->second();
    printf("PASS %s\n", name.c_str());
    return 0;
  } catch (const TestSkip &s) {
    printf("SKIP %s (%s)\n", name.c_str(), s.reason.c_str());
    return 77;
  } catch (const TestFailure &f) {
    printf("FAIL %s: %s\n", name.c_str(), f.message.c_str());
    return 1;
  } catch (const std::exception &e) {
    printf("FAIL %s: uncaught exception: %s\n", name.c_str(), e.what());
    return 1;
  } catch (...) {
    printf("FAIL %s: uncaught non-exception\n", name.c_str());
    return 1;
  }
}

inline int run_all_tests() {
  install_timeout(60);
  int passed = 0, failed = 0, skipped = 0;
  for (auto &[name, fn] : test_registry()) {
    try {
      fn();
      printf("PASS %s\n", name.c_str());
      ++passed;
    } catch (const TestSkip &s) {
      printf("SKIP %s (%s)\n", name.c_str(), s.reason.c_str());
      ++skipped;
    } catch (const TestFailure &f) {
      printf("FAIL %s: %s\n", name.c_str(), f.message.c_str());
      ++failed;
    } catch (const std::exception &e) {
      printf("FAIL %s: uncaught exception: %s\n", name.c_str(), e.what());
      ++failed;
    } catch (...) {
      printf("FAIL %s: uncaught non-exception\n", name.c_str());
      ++failed;
    }
  }
  printf("%d passed, %d failed, %d skipped\n", passed, failed, skipped);
  return failed == 0 ? 0 : 1;
}
