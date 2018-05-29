LLVM IR Trace Profiler (LLVM-Tracer) 2.0
========================================
LLVM-Tracer is an LLVM instrumentation pass to print out a dynamic LLVM IR
trace, including dynamic register values and memory addresses.

If you use LLVM-Tracer in your research, please cite:

ISA-Independent Workload Characterization and its Implications for Specialized
Architectures,
Yakun Sophia Shao and David Brooks,
International Symposium on Performance Analysis of Systems and Software
(ISPASS), April 2013


Requirements:
-------------------
  1. LLVM 6.0 and Clang 6.0 64-bit. It is highly recommended that users
     download the packages prebuilt by LLVM and install with the system's
     package manager (apt/yum/etc), rather than building the toolchain from
     source.
  2. GCC 5.4 or later.
  3. CMake 2.8.12 or newer.

Build:
-----------------
**Breaking changes from v1.2 to v2.0:**

  * LLVM-Tracer now uses LLVM 6.0. Support for all previous versions of LLVM
    have been removed. Minor versions 6.0.0 and 6.0.1 have been tested to work.
  * C++ is now supported in un-instrumented modes. That is, you can write C++
    to build your applications and only have LLVM-Tracer instrument and generate
    dynamic traces for the parts that only use C features.

**Breaking changes from v1.1 to v1.2:**

  * CMake is no longer optional. LLVM-Tracer only supports CMake for building.
    This is because of a new Clangtool component (under `ast-pass/`) which
    requires CMake.
  *  LLVM-Tracer will install all built files to the subdirectories `bin/` and
    `lib/` instead of `full-trace/` and `profile-func/`. The `BUILD_ON_SOURCE`
    option has been removed.

  CMake is a configure tool which allows you to do out-of-source build.
  LLVM-Tracer requires CMake newer than 2.8.12. By default, CMake
  searches for LLVM 6.0.

  1. Set `LLVM_HOME` to where you installed LLVM
     ```
     export LLVM_HOME=/path/to/your/llvm/installation
     ```

  2. Clone LLVM-Tracer.

     ```
     git clone https://github.com/ysshao/LLVM-Tracer
     cd LLVM-Tracer/
     ```
  3. Configure with CMake and build LLVM-Tracer source code.

     If you have LLVM installed:
     ```
     mkdir build/
     cd build
     cmake ..
     make
     make install
     ```

     If you want CMake to install LLVM for you (CAUTION: takes an hour!):
     ```
     mkdir build/
     cd build
     cmake .. -DLLVM_ROOT=/where/to/install/LLVM -DAUTOINSTALL=TRUE
     make
     make install
     ```

  3. (Optional) Try running triad example, which is built by CMake.
     ```
     cd /path/to/build/LLVM-Tracer/
     ctest -V
     ```

  4. Available CMake settings
     ```
     -DLLVM_ROOT=/where/your/llvm/install   (default : $LLVM_HOME)
       You may denote the path of LLVM to find/install by this option. if
       this option is not defined, environment variable LLVM_HOME will be
       used.

     -DAUTOINSTALL=TRUE,FALSE    (default : FALSE)
       By this option, CMake scripts will automatically download, build and
       install LLVM for you if finds no LLVM installation. Using this
       function requires tar-1.22 or newer to extract xz format.

       The default installation path is under /your/build_dir/lib/llvm-3.x.
       You can manually define the installation path by
       -DLLVM_ROOT=/where/to/install.

     -DCMAKE_BUILD_TYPE=None,Debug,Release    (default : None)
       You can choose one from three of bulid types.
     ```


Run:
------
After you build LLVM-Tracer, you can use example triad programs in the example
directory to test LLVM-Tracer.

Example program: triad
----------------------
  1. Go to /your/path/to/LLVM-Tracer/example/triad
  2. Run LLVM-Tracer to generate a dynamic LLVM IR trace
     a. LLVM-Tracer tracks regions of interest inside a program.
        In the triad example, we want to analyze the triad kernel instead of the setup
        and initialization work done in main. To tell LLVM-Tracer the functions we are
        interested in, set the enviroment variable WORKLOAD to be the top-level function name:

        ```
        export WORKLOAD=triad
        ```

        If you have multiple functions, separate them with commas.

        ```
        export WORKLOAD=md,md_kernel
        ```

        LLVM-Tracer will trace them differently based on the `-trace-all-callees` flag, which can be specified
        to the `opt` command (see step d).

        * If this flag is specified, then any function called by any function in the WORKLOAD variable will be traced.
          This is a simple way to trace multiple "top-level" functions at once.
        * If this flag is not specified, then only functions in the WORKLOAD variable will be traced.

     b. Generate the source code labelmap.

        ```
        export TRACER_HOME=/your/path/to/LLVM-Tracer
        ${TRACER_HOME}/bin/get-labeled-stmts triad.c -- -I${LLVM_HOME}/lib/clang/6.0/include
        ```

     c. Generate LLVM IR:

        ```
        clang -g -O1 -S -fno-slp-vectorize -fno-vectorize -fno-unroll-loops -fno-inline -emit-llvm -o triad.llvm triad.c
        ```

     d. Run LLVM-Tracer pass.
        Before you run, make sure you already built LLVM-Tracer and have set
        the environment variable `TRACER_HOME` to where you put LLVM-Tracer
        code.

        ```
        opt -S -load=${TRACER_HOME}/full-trace/full_trace.so -fulltrace -labelmapwriter [-trace-all-callees] triad.llvm -o triad-opt.llvm
        llvm-link -o full.llvm triad-opt.llvm ${TRACER_HOME}/profile-func/trace_logger.llvm
        ```

        The `-trace-all-callees` flag is optional and defaults to false.

     e. Generate machine code:

        ```
        llc -filetype=asm -o full.s full.llvm
        gcc -fno-inline -o triad-instrumented full.s
        ```

     f. Run binary. It will generate a file called `dynamic_trace` under current directory.

       ```
       ./triad-instrumented
       ```

     g. There is a script provided which performs all of these operations.

       ```
       python llvm_compile.py $TRACER_HOME/example/triad triad
       ```

`triad` is part of the SHOC benchmark suite. We provide a version of SHOC that
is ready to be used with LLVM-Tracer. Please go to
[Aladdin](https://github.com/ysshao/aladdin) and look under the `SHOC`
directory.

---------------------------------------------------------------------------------
Sophia Shao, Sam Xi, and Emma Wang
