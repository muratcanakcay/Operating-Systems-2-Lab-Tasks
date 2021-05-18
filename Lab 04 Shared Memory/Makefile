CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm -lrt

all: linear_stage5

linear_stage5: linear_stage5.c
	${CC} ${CFLAGS} -o linear_stage5 linear_stage5.c

.PHONY: clean all
clean:
	rm linear_stage5 
	