/* A small test case for the AST pass.
 *
 * foo_label should be resolved to line 20 and macro_label should be resolved to
 * line 15. We use the expansion location instead of the spelling location
 * because this is how tracer behaves.
 */

#include <stdio.h>

#define MACRO(x_ptr) \
  macro_label: *x_ptr = 5;

// Here, spelling location = 11, and expansion location = 15.
void bar(int* x) {
  MACRO(x);
}

// Here, spelling location = expansion location = 20.
void foo(int* x) {
foo_label: *x = 0;
}

void nested(int* x) {
  int i, j;
outer: for (i = 0; i < 10; i++) {
  inner: for (j = 0; j < 10; j++) {
        *x += i;
        *x += j;
   }
  }
}

// Here, label_here and label_in_between should all resolve to line 40.
void different_line(int* x) {
label_here:

label_in_between:
// comment in between

  *x = 1;
}

static void static_function(int *x) {
  static_loop:
  for (int i = 0; i < 10; i++) {
    *x += i;
  }
}

int main() {
  int x = 4;
  printf("x = %d\n", x);

  foo(&x);
  printf("x = %d\n", x);

  bar(&x);
  printf("x = %d\n", x);

  nested(&x);
  printf("x = %d\n", x);

  different_line(&x);
  printf("x = %d\n", x);

  static_function(&x);
  printf("x = %d\n", x);

  return 0;
}
