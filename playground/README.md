Playground tests for LLVM-Tracer
================================
Here we provide a few tests as usages of LLVM-Tracer.
1. test.c/test.cc: Examples of writing traced functions in C or C++ targets.
2. multithreading.cc: An example of tracing multithreaded programs, where each
   thread can generate its own trace.

Build:
--------------
To compile the trace binary for test.cc.
```
make trace-binary
```
Change the SUFFIX variable to switch between C and C++ targets (cc by default).
The below will build trace binary for test.c.
```
make trace-binary SUFFIX=c
```
To build for other files, change the EXEC variable. EXEC is the name of the
file.
```
make trace-binary EXEC=multithreading
```
You may also need to change the TOP_LEVEL variable, which sets the entry
function of your accelerator. For the triad example, it would be:
```
make trace-binary EXEC=triad TOP_LEVEL=triad SUFFIX=c
```
