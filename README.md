LLVM IR Trace Profiler (LLVM-Tracer) 1.1 Public Release
============================================
LLVM-Tracer is an LLVM instrumentation pass to print out a dynamic LLVM IR
trace, including dynamic register values and memory addresses.

If you use LLVM-Tracer in your research, please cite:

ISA-Independent Workload Characterization and its Implications for Specialized
Architectures,
Yakun Sophia Shao and David Brooks,
International Symposium on Performance Analysis of Systems and Software
(ISPASS), April 2013

============================================
Requirements:
-------------------
  1. LLVM 3.4 and Clang 3.4 64-bit or LLVM 3.5 and Clang 3.5 64-bit
  2. GCC 4.7 or newer for C++11 features.
  3. (Optional) CMake 2.8.12 or newer to use the CMake flow.

Build:
-----------------
  LLVM-Tracer currently supports two ways to build.

  1. Configure with CMake, then build with Make :

       If you are familiar with CMake, or you are interested in a script
       which can build out-of-source and offer automatical LLVM/Clang
       installation, choose [CMake](#build-with-cmake)

  2. Directly build with Makefile :

       If you want to keep all the things simple, or you do not have
       CMake, choose [Makefile](#build-with-makefile)


Build with CMake:
-----------------
  CMake is a configure tool which allows you to out-of-source build.
  LLVM-Tracer Requires CMake newer than 2.8.12. By default, CMake
  searches for LLVM 3.4.

  Note : In order to being compatiable with legacy work flow,
  CMake will bulid full-trace/full\_trace.so & profile-func/trace\_logger.llvm
  under source directory. To turn off, -DBUILD\_ON\_SOURCE=FALSE

  1. Set `LLVM_HOME` to where you installed LLVM
     ```
     export LLVM_HOME=/path/to/your/llvm/installation
     ```

  2. Configure with CMake and build LLVM-Tracer source code

     If you have LLVM installed :
     ```
     mkdir /path/to/build/LLVM-Tracer/
     cd /path/to/build/LLVM-Tracer/
     cmake /path/to/LLVM-Tracer/
     make
     ```

     If you want CMake to install LLVM for you. CAUTION : takes an hour!
     ```
     mkdir /path/to/build/LLVM-Tracer/
     cd /path/to/build/LLVM-Tracer/
     cmake /path/to/LLVM-Tracer/ -DLLVM_ROOT=/where/to/install/LLVM -DAUTOINSTALL=TRUE
     make
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

     -DBUILD_ON_SOURCE=TRUE,FALSE    (default : TRUE)
       By assign this option, CMake will build fulltrace.so &
       trace_logger.llvm under the source directory.
       Other llvm bitcode and object files still remain in the build directory.

     ```

Build with Makefile:
---------------------

  1. Set `LLVM_HOME` to where you installed LLVM
     Add LLVM and Clang binary to your PATH:

     ```
     export LLVM_HOME=/path/to/your/llvm/installation
     export PATH=$LLVM_HOME/bin:$PATH
     export LD_LIBRARY_PATH=$LLVM_HOME/lib/:$LD_LIBRARY_PATH
     ```
     Also set `GCC_INSTALL_HOME` to where you installed gcc4.7+
     ```
     export GCC_INSTALL_HOME=/path/to/your/gcc-4.7-or-up/installation
     ```

  2. Go to where you put LLVM-Tracer source code

     ```
     cd /path/to/LLVM-Tracer/full-trace/
     make
     cd /path/to/LLVM-Tracer/profile-func/
     make
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
        and initialization work done in main.
        To tell LLVM-Tracer the functions we are
        interested in, set enviroment variable WORKLOAD to be the function names: 
        export WORKLOAD=triad

    (if you have multiple functions you are interested in, separate with commas):

        export WORKLOAD=md,md_kernel

     b. Generate LLVM IR:

        clang -g -O1 -S -fno-slp-vectorize -fno-vectorize -fno-unroll-loops -fno-inline -emit-llvm -o triad.llvm triad.c

     c. Run LLVM-Tracer pass:
        Before you run, make sure you already built LLVM-Tracer.
        Set `$TRACER_HOME` to where you put LLVM-Tracer code.


        export TRACER_HOME=/your/path/to/LLVM-Tracer
        opt -S -load=$TRACER_HOME/full-trace/full_trace.so -fulltrace triad.llvm -o triad-opt.llvm
        llvm-link -o full.llvm triad-opt.llvm $TRACER_HOME/profile-func/trace_logger.llvm


     d. Generate machine code:


        llc -filetype=asm -o full.s full.llvm
        gcc -fno-inline -o triad-instrumented full.s


     e. Run binary. It will generate a file called `dynamic_trace` under current directory.

        ./triad-instrumented


     f. We provide a python script to run the above steps automatically for SHOC.


        cd /your/path/to/LLVM-Tracer/triad
        export TRACER_HOME=/your/path/to/LLVM-Tracer
        python llvm_compile.py $TRACER_HOME/example/triad triad

---------------------------------------------------------------------------------
Emma Wang and Sophia Shao

Harvard University, 2014
