# Create 32-bit executable for fib.c
CC = gcc
CFLAGS = -m32 -no-pie -nostdlib

all: fib

fib: fib.c
	$(CC) $(CFLAGS) -o fib fib.c

clean:
	rm -f fib