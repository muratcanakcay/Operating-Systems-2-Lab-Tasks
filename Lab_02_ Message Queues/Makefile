CC=gcc
CFLAGS= -std=gnu99 -Wall -lm -lrt

all: prog1_stage1 

prog1_stage1: prog1_stage1.c
	gcc -o prog1_stage1 prog1_stage1.c -lpthread -lm -lrt

.PHONY: clean all
clean:
	rm prog1_stage1