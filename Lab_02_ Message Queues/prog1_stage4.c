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
	fprintf(stderr,"USAGE: prog1_stage2 q0_name \n");	
	exit(EXIT_FAILURE);
}

void sigchld_handler(int sig) {
	pid_t pid;	

	for(;;)
    {
		pid=waitpid(0, NULL, WNOHANG);
		if(pid>0){
            
            puts("child terminated");
        }
        if(pid == 0) return;
		if(pid <= 0) 
        {
			if(errno==ECHILD) return;
			ERR("waitpid");
		}
	}
	
}

void sethandler( void (*f)(int), int sigNo) {
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1==sigaction(sigNo, &act, NULL)) ERR("sigaction");
}

void child_work(char* register_pid);

int main(int argc, char** argv) 
{
	if(argc!=2) usage();

	char q0_name[MAXLENGTH];
    snprintf(q0_name, MAXLENGTH, "/%s", argv[1]);
	
	mqd_t q0; // descriptors
	struct mq_attr attr; // attributes
	attr.mq_maxmsg = 10; // capacity
	attr.mq_msgsize = MAXLENGTH; // max msg size in bytes
	if((q0=TEMP_FAILURE_RETRY(mq_open(q0_name, O_RDWR | O_CREAT, 0600, &attr)))==(mqd_t)-1) ERR("prog2 mq_open q0");

	printf("prog1: message queue q0 with name: %s opened\n", q0_name);

    sethandler(sigchld_handler,SIGCHLD);
	
    pid_t pid;
    
    while(1)
    {
        char buffer[MAXLENGTH];
        mq_receive(q0, buffer, MAXLENGTH ,NULL);

        buffer[sizeof(buffer)-1]='\0';
        puts(buffer);
        pid_t pid;
        if((pid=fork())<0) ERR("fork");
        if(0==pid) 
        child_work(buffer+9);
    }
	
	
	mq_close(q0);
	//if(mq_unlink(q0_name))ERR("mq unlink");

	puts("prog 1 EXITING");
	
	return EXIT_SUCCESS;
}

void child_work(char* register_pid)
{
    char q_name[MAXLENGTH];
    snprintf(q_name, MAXLENGTH, "/q%s", register_pid);
	
	mqd_t q; // descriptors
	struct mq_attr attr; // attributes
	attr.mq_maxmsg = 10; // capacity
	attr.mq_msgsize = MAXLENGTH; // max msg size in bytes
	if((q = TEMP_FAILURE_RETRY(mq_open(q_name, O_RDWR | O_CREAT, 0600, &attr)))==(mqd_t)-1) ERR("prog1 child mq_open q");

	printf("prog1 child: message queue with name: %s opened\n", q_name);

    if(TEMP_FAILURE_RETRY(mq_send(q, "check status", MAXLENGTH, 0))) ERR("mq_send");
	
	printf("prog1 child: message sent: \"check status\n\"");

    puts("prog 1 child EXITING");
}