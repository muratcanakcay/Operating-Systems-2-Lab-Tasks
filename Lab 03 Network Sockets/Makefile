CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm -lrt

all: udpfwd_stage6 udpfwd_stage5 udpfwd_stage4 udpfwd_stage3 udpfwd_stage2 udpfwd_stage1

udpfwd_stage1: udpfwd_stage1.c
	${CC} ${CFLAGS} -o udpfwd_stage1 udpfwd_stage1.c
	
udpfwd_stage2: udpfwd_stage2.c
	${CC} ${CFLAGS} -o udpfwd_stage2 udpfwd_stage2.c

udpfwd_stage3: udpfwd_stage3.c
	${CC} ${CFLAGS} -o udpfwd_stage3 udpfwd_stage3.c

udpfwd_stage4: udpfwd_stage4.c
	${CC} ${CFLAGS} -o udpfwd_stage4 udpfwd_stage4.c

udpfwd_stage5: udpfwd_stage5.c
	${CC} ${CFLAGS} -o udpfwd_stage5 udpfwd_stage5.c

udpfwd_stage6: udpfwd_stage6.c
	${CC} ${CFLAGS} -o udpfwd_stage6 udpfwd_stage6.c

.PHONY: clean all
clean:
	rm udpfwd_stage6 udpfwd_stage5 udpfwd_stage4 udpfwd_stage3 udpfwd_stage2 udpfwd_stage1