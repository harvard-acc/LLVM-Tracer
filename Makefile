.PHONY: all

all:
	$(MAKE) -C full-trace
	$(MAKE) -C profile-func

clean:
	$(MAKE) -C full-trace clean
	$(MAKE) -C profile-func clean
