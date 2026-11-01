#Create lib_simpleloader.so from loader.c

#Provide the command for cleanup
CC = gcc
CFLAGS = -m32 -fPIC

all: ../bin/lib_simpleloader.so

../bin/lib_simpleloader.so: loader.o
	$(CC) -m32 -shared -o ../bin/lib_simpleloader.so loader.o

loader.o: loader.c loader.h
	$(CC) $(CFLAGS) -c loader.c -o loader.o

clean:
	rm -f loader.o ../bin/lib_simpleloader.so