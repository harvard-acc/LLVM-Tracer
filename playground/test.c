/* A preprepared environment to test out LLVM-Tracer.
 *
 * Feel free to do anything you like in this file, but make sure you update the
 * WORKLOAD environment variable in the Makefile if you change the name of the
 * top level function.
 */

#include <stdio.h>

int top_level(int a, int b) {
  return a + b;
}

int main() {
  int c = top_level(1, 2);
  printf("Result = %d\n", c);
  return 0;
}
