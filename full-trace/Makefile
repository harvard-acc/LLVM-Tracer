LLVM_HOME?=PLEASE_SET_LLVM_HOME_ENVIRONMENT_VARIABLE
LLVM_BIN=$(LLVM_HOME)/
#LLVM_BIN=$(LLVM_HOME)/llvm-bin

all: full_trace.so

SYSTEM=$(shell uname)
ifeq ($(SYSTEM), Darwin)
	CFLAGS=  -fno-rtti -fno-common  -I/usr/include/c++/4.2.1/ \
           -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS \
           -DLLVM_VERSION="34" \
           -I$(LLVM_HOME)/include/llvm -I$(LLVM_HOME)/include/llvm-c
	LDFLAGS=  -dynamiclib -Wl,-flat_namespace -Wl,-undefined,suppress
endif
ifeq ($(SYSTEM), Linux)
	CFLAGS= -fno-rtti -O3 -fPIC -I/usr/include/c++/4.2.1/  \
          -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS \
          -DLLVM_VERSION="34" \
          -I$(LLVM_HOME)/include/llvm -I$(LLVM_HOME)/include/llvm-c

	LDFLAGS= -shared
endif

full_trace.so  : %.so: %.cpp
	$(LLVM_BIN)/bin/clang++ -v $(CFLAGS) -c $< -o tmp.o `$(LLVM_BIN)/bin/llvm-config --cxxflags`
	$(LLVM_BIN)/bin/clang++ -v $(LDFLAGS) -o $@ tmp.o
	rm -f tmp.o

clean:
	rm -f full_trace.so
