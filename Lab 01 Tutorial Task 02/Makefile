CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm

all: prog22a prog22b

prog22a: prog22a.c
	${CC} ${CFLAGS} -o prog22a prog22a.c

prog22b: prog22b.c
	${CC} ${CFLAGS} -o prog22b prog22b.c

.PHONY: clean all
clean:
	rm prog22a prog22b
	