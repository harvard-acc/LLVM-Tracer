LLVM IR Trace Profiler (LLVM-Tracer) 1.2
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
  1. LLVM 3.4 and Clang 3.4 64-bit or LLVM 3.5 and Clang 3.5 64-bit
  2. GCC 4.7 or newer for C++11 features.
  3. CMake 2.8.12 or newer.

Build:
-----------------
**Breaking changes from v1.1 to v1.2:**

  * CMake is no longer optional. LLVM-Tracer only supports CMake for building.
    This is because of a new Clangtool component (under `ast-pass/`) which
    requires CMake.
  *  LLVM-Tracer will install all built files to the subdirectories `bin/` and
    `lib/` instead of `full-trace/` and `profile-func/`. The `BUILD_ON_SOURCE`
    option has been removed.

  CMake is a configure tool which allows you to out-of-source build.
  LLVM-Tracer requires CMake newer than 2.8.12. By default, CMake
  searches for LLVM 3.4.

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

     -DLLVM_RECOMMEND_VERSION="3.4", "3.5"    (default : 3.4)
       LLVM-Tracer supports both LLVM 3.4 and 3.5. It uses LLVM 3.4 by
       default, but you can manually specify the LLVM version to use.

     -DAUTOINSTALL=TRUE,FALSE    (default : FALSE)
       By this option, CMake scripts will automatically download, build and
       install LLVM for you if finds no LLVM installation. Using this
       function requires tar-1.22 or newer to extract xz format.

       The default installation path is under /your/build_dir/lib/llvm-3.x.
       You can manually define the installation path by
       -DLLVM_ROOT=/where/to/install.

       The default installation version will be 3.4. You can define
       the installation version by -DLLVM_RECOMMEND_VERSION="3.5" or "3.4".
       This auto install script will try to use the latest patch version
       according to cmake-scripts/AutoInstall/LLVMPatchVersion.cmake

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

        If you have multiple functions you are interested in (which are not
        called by the top-level function), separate with commas:

        ```
        export WORKLOAD=md,md_kernel
        ```

     b. Generate the source code labelmap.

        ```
        export TRACER_HOME=/your/path/to/LLVM-Tracer
        ${TRACER_HOME}/bin/get-labeled-stmts triad.c -- -I${LLVM_HOME}/lib/clang/3.4/include
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
        opt -S -load=${TRACER_HOME}/full-trace/full_trace.so -fulltrace -labelmapwriter triad.llvm -o triad-opt.llvm
        llvm-link -o full.llvm triad-opt.llvm ${TRACER_HOME}/profile-func/trace_logger.llvm
        ```

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
