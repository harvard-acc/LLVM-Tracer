#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include <iostream>

#include "../profile-func/trace_logger_aladdin.h"

#define NUM_THREADS 4

struct Args {
  int trace_idx;
  int a;
  int b;
  pthread_mutex_t *lock;
  Args(int _trace_idx, int _a, int _b, pthread_mutex_t *_lock)
      : trace_idx(_trace_idx), a(_a), b(_b), lock(_lock) {}
};

// Though we can write C++ code, only code with external C-style linkage will be
// instrumented (extern "C").
#ifdef __cplusplus
extern "C" {
#endif
int top_level(int a, int b) { return a + b; }
#ifdef __cplusplus
}
#endif

char* get_trace_name(int trace_idx) {
  char* buffer = (char*)malloc(30);
  snprintf(buffer, 30, "dynamic_trace_%d.gz", trace_idx);
  return buffer;
}

void *top_level_wrapper(void *args) {
  Args *trace_args = reinterpret_cast<Args *>(args);
  // This is a usage of the LLVM-Tracer API in order to specify the trace name
  // the current thread will generate.
  llvmtracer_set_trace_name(get_trace_name(trace_args->trace_idx));
  int c = top_level(trace_args->a, trace_args->b);
  pthread_mutex_lock(trace_args->lock);
  std::cout << "Trace " << trace_args->trace_idx << ", result = " << c << ".\n";
  pthread_mutex_unlock(trace_args->lock);
  delete trace_args;
  pthread_exit(NULL);
}

int main() {
  pthread_t threads[NUM_THREADS];
  pthread_mutex_t lock;
  pthread_mutex_init(&lock, NULL);
  for (int i = 0; i < NUM_THREADS; i++) {
    Args* args = new Args(i, i * 2, i * 2 + 1, &lock);
    pthread_create(&threads[i], NULL, top_level_wrapper, (void *)args);
  }
  for (int i = 0; i < NUM_THREADS; i++)
    pthread_join(threads[i], NULL);
  pthread_mutex_destroy(&lock);
  return 0;
}
