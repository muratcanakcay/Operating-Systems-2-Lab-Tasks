CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm -lrt

all: linear_stage3

linear_stage3: linear_stage3.c
	${CC} ${CFLAGS} -o linear_stage3 linear_stage3.c

.PHONY: clean all
clean:
	rm linear_stage3 
	