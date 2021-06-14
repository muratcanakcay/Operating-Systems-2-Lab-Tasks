CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm

all: pipefork_stage1

pipefork_stage1: pipefork_stage1.c
	${CC} ${CFLAGS} -o pipefork_stage1 pipefork_stage1.c

.PHONY: clean all
clean:
	rm pipefork_stage1 
	