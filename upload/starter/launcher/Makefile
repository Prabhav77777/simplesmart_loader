CC = gcc
CFLAGS = -m32 -Wall -Wextra
all: ../bin/launch
../bin/launch: launch.c ../loader/loader.h ../bin/lib_simpleloader.so
	mkdir -p ../bin
	$(CC) $(CFLAGS) -I../loader -o $@ launch.c -L../bin -l_simpleloader -Wl,-rpath,'$$ORIGIN'
clean:
	-@rm -f ../bin/launch
