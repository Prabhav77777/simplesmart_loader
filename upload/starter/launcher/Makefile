#Compile the launch.c by linking it with the lib_simpleloader.so

#Provide the command for cleanup
CC = gcc
CFLAGS = -m32 -I../loader

all: ../bin/launch

../bin/launch: launch.o ../bin/lib_simpleloader.so
	$(CC) -m32 -o ../bin/launch launch.o -L../bin -l_simpleloader -Wl,-rpath,'$$ORIGIN'

launch.o: launch.c ../loader/loader.h
	$(CC) $(CFLAGS) -c launch.c -o launch.o

clean:
	rm -f launch.o ../bin/launch