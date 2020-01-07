/* Testing that the AST pass works for .cpp files as well.
 *
 * Expected behavior: ignores all labels in functions not in extern "C" context.
 */

#include <stdio.h>
#include <stdlib.h>

extern "C" {

// Labels here *should* appear because it is in extern C context.
static void static_function(float *a, int size) {
    static_loop:
    for (int i = 0; i < size; i++) {
        if (a[i] < 0)
            a[i] = 0;
    }
}

// Labels here should appear.
void nonstatic_function(float *a, int size) {
    nonstatic_loop:
    for (int i = 0; i < size; i++) {
        if (a[i] < 0)
            a[i] = 0;
    }
}

}

// Outside of extern "C", should not appear.
void cxx_linkage_function(float *a, int size) {
    loop:
    for (int i = 0; i < size; i++) {
        if (a[i] < 0)
            a[i] = 0;
    }
}

namespace name {
void namespaced_func(float *a, int size) {
    loop:
    for (int i = 0; i < size; i++) {
        if (a[i] < 0)
            a[i] = 0;
    }
}

// Function templates should be ignored.
template <typename C>
void template_func(C& a) {
    a++;
}
}

class MyClass {
  public:
    MyClass() {
        a=3;
    }

    // Member functions are ignored.
    int loop() {
        loop:
        for (int i =0; i < 3; i++) {
            a += i;
        }
	return a;
    }

    int getA() { return a; }

  private:
    int a;
};

int main() {
    float a[4] = { -1, -2, 3, 4 };

    static_function(&a[0], 4);
    printf("a[0] = %f\n", a[0]);

    nonstatic_function(&a[0], 4);
    printf("a[1] = %f\n", a[1]);

    cxx_linkage_function(&a[0], 4);
    printf("a[2] = %f\n", a[2]);

    name::template_func<float>(a[3]);
    printf("a[3] = %f\n", a[3]);

    return 0;
}

