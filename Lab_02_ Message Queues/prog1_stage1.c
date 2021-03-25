#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>

#define MAXLENGTH 100

#define LIFE_SPAN 10
#define MAX_NUM 10

#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
		     		     exit(EXIT_FAILURE))



void usage(void){
	fprintf(stderr,"USAGE: prog1_stage1 q0_name \n");	
	exit(EXIT_FAILURE);
}

int main(int argc, char** argv) 
{
	if(argc!=2) usage();

	char q0_name[MAXLENGTH];
    snprintf(q0_name, MAXLENGTH, "/%s", argv[1]);
	
	mqd_t q0; // descriptors
	struct mq_attr attr; // attributes
	attr.mq_maxmsg = 10; // capacity
	attr.mq_msgsize = MAXLENGTH; // max msg size in bytes
	if((q0=TEMP_FAILURE_RETRY(mq_open(q0_name, O_RDWR | O_NONBLOCK | O_CREAT, 0600, &attr)))==(mqd_t)-1) ERR("mq open q0");

	printf("message queue q0 with name: %s opened\n", q0_name);
	char buffer[MAXLENGTH];

	if(TEMP_FAILURE_RETRY(mq_send(q0, "This is the message", 19, 0))) ERR("mq_send");
	if(TEMP_FAILURE_RETRY(mq_receive(q0, buffer, MAXLENGTH ,NULL)) < 1) ERR("mq_receive");
	puts(buffer);

	
	mq_close(q0);
	if(mq_unlink(q0_name))ERR("mq unlink");
	
	return EXIT_SUCCESS;
}