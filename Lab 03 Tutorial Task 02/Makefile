CC=gcc
CFLAGS= -std=gnu99 -Wall -lm -lrt

all: prog_c prog_s

prog_c: prog_c.c
	gcc -o prog_c prog_c.c -Wall -lpthread -lm

prog_s: prog_s.c
	gcc -o prog_s prog_s.c -Wall -lpthread -lm

.PHONY: clean all
clean:
	rm prog_s prog_c