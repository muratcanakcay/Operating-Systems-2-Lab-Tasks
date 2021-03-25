CC=gcc
CFLAGS= -std=gnu99 -Wall -lm -lrt

all: prog1_stage1 prog1_stage2 prog2_stage2 prog1_stage3 prog2_stage3 prog1_stage4 prog2_stage4

prog1_stage1: prog1_stage1.c
	gcc -o prog1_stage1 prog1_stage1.c -lpthread -lm -lrt

prog1_stage2: prog1_stage2.c
	gcc -o prog1_stage2 prog1_stage2.c -lpthread -lm -lrt

prog2_stage2: prog2_stage2.c
	gcc -o prog2_stage2 prog2_stage2.c -lpthread -lm -lrt

prog1_stage3: prog1_stage3.c
	gcc -o prog1_stage3 prog1_stage3.c -lpthread -lm -lrt

prog2_stage3: prog2_stage3.c
	gcc -o prog2_stage3 prog2_stage3.c -lpthread -lm -lrt

prog1_stage4: prog1_stage4.c
	gcc -o prog1_stage4 prog1_stage4.c -lpthread -lm -lrt

prog2_stage4: prog2_stage4.c
	gcc -o prog2_stage4 prog2_stage4.c -lpthread -lm -lrt

.PHONY: clean all
clean:
	rm prog1_stage1 prog1_stage2 prog2_stage2 prog1_stage3 prog2_stage3 prog1_stage4 prog2_stage4