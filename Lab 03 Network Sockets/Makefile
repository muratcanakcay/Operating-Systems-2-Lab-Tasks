CC=gcc
CFLAGS= -std=gnu99 -Wall -lm -lrt

all: udpfwd_stage5 udpfwd_stage4 udpfwd_stage3 udpfwd_stage2 udpfwd_stage1

udpfwd_stage1: udpfwd_stage1.c
	gcc -o udpfwd_stage1 udpfwd_stage1.c -Wall -lpthread -lm
	
udpfwd_stage2: udpfwd_stage2.c
	gcc -o udpfwd_stage2 udpfwd_stage2.c -Wall -lpthread -lm

udpfwd_stage3: udpfwd_stage3.c
	gcc -o udpfwd_stage3 udpfwd_stage3.c -Wall -lpthread -lm

udpfwd_stage4: udpfwd_stage4.c
	gcc -o udpfwd_stage4 udpfwd_stage4.c -Wall -lpthread -lm

udpfwd_stage5: udpfwd_stage5.c
	gcc -o udpfwd_stage5 udpfwd_stage5.c -Wall -lpthread -lm

.PHONY: clean all
clean:
	rm udpfwd_stage5 udpfwd_stage4 udpfwd_stage3 udpfwd_stage2 udpfwd_stage1