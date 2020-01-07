/* A preprepared environment to test out LLVM-Tracer using C++.
 *
 * Feel free to do anything you like in this file, but make sure you update the
 * WORKLOAD environment variable in the Makefile if you change the name of the
 * top level function.
 */
#include <iostream>

// Though we can write C++ code, only code with external C-style linkage will be
// instrumented (extern "C").
#ifdef __cplusplus
extern "C" {
#endif
int top_level(int a, int b) { return a + b; }
#ifdef __cplusplus
}
#endif

class Adder {
 public:
  Adder(int _a, int _b) : a(_a), b(_b) {}
  int run() {
    // The traced function needs to be pure C.
    return top_level(a, b);
  }

 private:
  int a;
  int b;
};

int main() {
  Adder adder(1, 2);
  int c = adder.run();
  std::cout << "Result = " << c << "\n";
  return 0;
}
