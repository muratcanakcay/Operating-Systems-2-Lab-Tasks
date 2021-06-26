CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm

all: cas_list

cas_list: cas_list.c
	${CC} ${CFLAGS} -o cas_list cas_list.c

.PHONY: clean all 
clean:
	rm cas_list
	