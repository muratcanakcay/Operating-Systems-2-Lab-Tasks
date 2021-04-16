CC=gcc
CFLAGS= -std=gnu99 -Wall -lm -lrt

all: prog23a_s prog23b_s prog23_local prog23_tcp

prog23a_s: prog23a_s.c
	gcc -o prog23a_s prog23a_s.c -Wall -lpthread -lm

prog23b_s: prog23b_s.c
	gcc -o prog23b_s prog23b_s.c -Wall -lpthread -lm

prog23_local: prog23_local.c
	gcc -o prog23_local prog23_local.c -Wall -lpthread -lm

prog23_tcp: prog23_tcp.c
	gcc -o prog23_tcp prog23_tcp.c -Wall -lpthread -lm

.PHONY: clean all
clean:
	rm prog23a_s prog23b_s prog23_tcp prog23_local