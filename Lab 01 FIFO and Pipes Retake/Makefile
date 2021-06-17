CC=gcc
CFLAGS= -std=gnu99 -Wall -lpthread -lm

all: pipefork_stage1 pipefork_stage2 pipefork_stage3 pipefork_stage4 pipefork_stage5 pipefork_stage6

pipefork_stage1: pipefork_stage1.c
	${CC} ${CFLAGS} -o pipefork_stage1 pipefork_stage1.c

pipefork_stage2: pipefork_stage2.c
	${CC} ${CFLAGS} -o pipefork_stage2 pipefork_stage2.c

pipefork_stage3: pipefork_stage3.c
	${CC} ${CFLAGS} -o pipefork_stage3 pipefork_stage3.c

pipefork_stage4: pipefork_stage4.c
	${CC} ${CFLAGS} -o pipefork_stage4 pipefork_stage4.c

pipefork_stage5: pipefork_stage5.c
	${CC} ${CFLAGS} -o pipefork_stage5 pipefork_stage5.c

pipefork_stage6: pipefork_stage6.c
	${CC} ${CFLAGS} -o pipefork_stage6 pipefork_stage6.c

.PHONY: clean all 
clean:
	rm pipefork_stage1 pipefork_stage2 pipefork_stage3 pipefork_stage4 pipefork_stage5 pipefork_stage6 
	