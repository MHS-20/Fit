// Fit test runner entry point.
#include "framework.hpp"

int main(int argc, char **argv) {
  if (argc >= 2)
    return run_named_test(argv[1]);
  return run_all_tests();
}
