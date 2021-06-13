CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm

all: prog21a_s prog21b_s prog21_c

prog21a_s: prog21a_s.c
	${CC} ${CFLAGS} -o prog21a_s prog21a_s.c

prog21b_s: prog21b_s.c
	${CC} ${CFLAGS} -o prog21b_s prog21b_s.c

prog21_c: prog21_c.c
	${CC} ${CFLAGS} -o prog21_c prog21_c.c

.PHONY: clean all
clean:
	rm prog21a_s prog21b_s prog21_c 
	