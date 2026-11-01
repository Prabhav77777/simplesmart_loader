.PHONY: all clean check

all:
	$(MAKE) -C loader
	$(MAKE) -C launcher
	$(MAKE) -C test

check: all
	./bin/launch ./test/fib
	./bin/launch ./test/sum

clean:
	$(MAKE) -C loader clean
	$(MAKE) -C launcher clean
	$(MAKE) -C test clean