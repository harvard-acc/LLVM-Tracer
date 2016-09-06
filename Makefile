.PHONY: all install clean

all:
	$(MAKE) -C full-trace
	$(MAKE) -C profile-func

install: all
	mkdir -p ${TRACER_HOME}/install/bin
	mkdir -p ${TRACER_HOME}/install/lib
	cp ${TRACER_HOME}/full-trace/full_trace.so ${TRACER_HOME}/install/lib
	cp ${TRACER_HOME}/profile-func/trace_logger.llvm ${TRACER_HOME}/install/lib

clean:
	$(MAKE) -C full-trace clean
	$(MAKE) -C profile-func clean
