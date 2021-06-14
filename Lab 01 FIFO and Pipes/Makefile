CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm

all: pipefork_stage2

pipefork_stage2: pipefork_stage2.c
	${CC} ${CFLAGS} -o pipefork_stage2 pipefork_stage2.c

.PHONY: clean all
clean:
	rm pipefork_stage2 
	