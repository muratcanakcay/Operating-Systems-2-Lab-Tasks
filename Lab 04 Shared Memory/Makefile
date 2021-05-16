CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm -lrt

all: linear_stage1

linear_stage1: linear_stage1.c
	${CC} ${CFLAGS} -o linear_stage1 linear_stage1.c

.PHONY: clean all
clean:
	rm linear_stage1 
	