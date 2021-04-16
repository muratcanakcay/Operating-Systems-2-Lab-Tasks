CC=gcc
CFLAGS= -std=gnu99 -Wall -lm -lrt

all: prog24c prog24s

prog24c: prog24c.c
	gcc -o prog24c prog24c.c -Wall -lpthread -lm

prog24s: prog24s.c
	gcc -o prog24s prog24s.c -Wall -lpthread -lm

.PHONY: clean all
clean:
	rm prog24s prog24c