CC=gcc
CFLAGS= -std=gnu99 -Wall -lrt

all: mq_example

mq_example:mq_example.c
	gcc -Wall -o mq_example mq_example.c -std=gnu99 -lrt

.PHONY: clean all
clean:
	rm mq_example