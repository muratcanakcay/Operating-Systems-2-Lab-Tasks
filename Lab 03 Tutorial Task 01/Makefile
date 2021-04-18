CC=gcc
CFLAGS= -std=gnu99 -Wall -lm -lrt

all: prog1_s prog2_s prog1_local prog2_tcp

prog1_s: prog1_s.c
	gcc -o prog1_s prog1_s.c -Wall -lpthread -lm

prog2_s: prog2_s.c
	gcc -o prog2_s prog2_s.c -Wall -lpthread -lm

prog1_local: prog1_local.c
	gcc -o prog1_local prog1_local.c -Wall -lpthread -lm

prog2_tcp: prog2_tcp.c
	gcc -o prog2_tcp prog2_tcp.c -Wall -lpthread -lm

.PHONY: clean all
clean:
	rm prog1_s prog2_s prog2_tcp prog1_local