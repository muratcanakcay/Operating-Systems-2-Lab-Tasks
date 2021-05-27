CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm -lrt

all: linear_stage6

linear_stage6: linear_stage6.c
	${CC} ${CFLAGS} -o linear_stage6 linear_stage6.c

.PHONY: clean all
clean:
	rm linear_stage6 
	