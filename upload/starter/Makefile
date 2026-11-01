.PHONY: all clean check
all:
	$(MAKE) -C loader
	$(MAKE) -C launcher
	$(MAKE) -C test
check: all
	bash test/extra/run_tests.sh
clean:
	$(MAKE) -C loader clean
	$(MAKE) -C launcher clean
	$(MAKE) -C test clean
	-@rm -rf bin
