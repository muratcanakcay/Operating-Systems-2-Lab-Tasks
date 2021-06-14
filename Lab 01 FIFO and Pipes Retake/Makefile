CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm

all: pipefork_stage3

pipefork_stage3: pipefork_stage3.c
	${CC} ${CFLAGS} -o pipefork_stage3 pipefork_stage3.c

.PHONY: clean all
clean:
	rm pipefork_stage3 
	