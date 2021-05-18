CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm -lrt

all: linear_stage4

linear_stage4: linear_stage4.c
	${CC} ${CFLAGS} -o linear_stage4 linear_stage4.c

.PHONY: clean all
clean:
	rm linear_stage4 
	