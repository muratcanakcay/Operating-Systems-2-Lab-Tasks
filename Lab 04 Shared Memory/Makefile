CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm -lrt

all: linear_stage2

linear_stage2: linear_stage2.c
	${CC} ${CFLAGS} -o linear_stage2 linear_stage2.c

.PHONY: clean all
clean:
	rm linear_stage2 
	