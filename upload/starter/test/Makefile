CC = gcc
CFLAGS = -m32 -no-pie -nostdlib -Wall -Wextra
all: fib sum
fib: fib.c
	$(CC) $(CFLAGS) -o fib fib.c
sum: sum.c
	$(CC) $(CFLAGS) -o sum sum.c
clean:
	-@rm -f fib sum
