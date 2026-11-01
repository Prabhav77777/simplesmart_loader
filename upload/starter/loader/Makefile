CC = gcc
CFLAGS = -m32 -Wall -Wextra -fPIC -O2
all: ../bin/lib_simpleloader.so
../bin/lib_simpleloader.so: loader.c loader.h
	mkdir -p ../bin
	$(CC) $(CFLAGS) -shared -Wl,-z,now -o $@ loader.c
clean:
	-@rm -f ../bin/lib_simpleloader.so
