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
  LLVM 3.4 and Clang 3.4 64-bit

Build:
------
  1. Set `LLVM_HOME` to where you installed LLVM
     Add LLVM and Clang binary to your PATH:

     ```
     export $LLVM_HOME=/path/to/your/llvm/installation
     export $PATH=$LLVM_HOME/bin:$PATH
     export $LD_LIBRARY_PATH=$LLVM_HOME/lib/:$LD_LIBRARY_PATH
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
        interested, set enviroment variable WORKLOAD to be the function names (if you
        have multiple functions interested, separated by comma):

        export WORKLOAD=triad
        export WORKLOAD=md,md_kernel

     b. Generate LLVM IR:

        clang -g -O1 -S -fno-slp-vectorize -fno-vectorize -fno-unroll-loops -fno-inline -emit-llvm -o triad.llvm triad.c

     c. Run LLVM-Tracer pass:
        Before you run, make sure you already built LLVM-Tracer.
        Set `$TRACER_HOME` to where you put LLVM-Tracer code.


        export TRACER_HOME=/your/path/to/LLVM-Tracer
        opt -S -load=$TRACER_HOME/full-trace/full_trace.so -full trace triad.llvm -o triad-opt.llvm
        llvm-link -o full.llvm triad-opt.llvm $TRACER_HOME/profile-func/tracer_logger.llvm


     d. Generate machine code:


        llc -filetype=asm -o ful.s full.llvm
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
