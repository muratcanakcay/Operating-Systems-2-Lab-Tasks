CC=gcc
CFLAGS= -std=gnu99 -Wall -lm -lrt

all: udpfwd_stage2 udpfwd_stage1

udpfwd_stage1: udpfwd_stage1.c
	gcc -o udpfwd_stage1 udpfwd_stage1c -Wall -lpthread -lm
	
udpfwd_stage2: udpfwd_stage2.c
	gcc -o udpfwd_stage2 udpfwd_stage2.c -Wall -lpthread -lm

.PHONY: clean all
clean:
	rm udpfwd_stage2 udpfwd_stage1