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

#define HERE puts("**************HERE***************");
#define DEBUG 0
#define MAXLENGTH 100
#define MAXCAPACITY 10
#define MSG_CHECK_STATUS "check status"
#define MSG_REGISTER "register"


#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
		     		     exit(EXIT_FAILURE))

void usage(void) // TODO: update for stage 4
{
	fprintf(stderr,"USAGE: prog1_stage2 q0_name \n");	
	exit(EXIT_FAILURE);
}

void sigchld_handler(int sig) 
{
	pid_t pid;	

	for(;;)
    {
		pid=waitpid(0, NULL, WNOHANG);
		if(pid > 0 && DEBUG) puts("[DEBUG] Prog1 child terminated");
        if(pid == 0) return;
		if(pid <= 0) 
        {
			if(errno==ECHILD) return;
			ERR("waitpid");
		}
	}
}

void sethandler( void (*f)(int), int sigNo) 
{
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1==sigaction(sigNo, &act, NULL)) ERR("sigaction");
}

void child_work(char* register_pid)
{
    char q_name[MAXLENGTH];

	mqd_t q;
	struct mq_attr attr; 
	attr.mq_maxmsg = MAXCAPACITY; 
	attr.mq_msgsize = MAXLENGTH; 
	
	// open /q<PID>
	snprintf(q_name, MAXLENGTH, "/q%s", register_pid);
	if( (q = TEMP_FAILURE_RETRY(mq_open(q_name, O_WRONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1) ERR("prog1 child mq_open q");
	
	// send "check status" message to prog2
	if(TEMP_FAILURE_RETRY(mq_send(q, MSG_CHECK_STATUS, strlen(MSG_CHECK_STATUS), 0))) ERR("prog1 child mq_send");
	printf("Message sent on %s : \"%s\"\n", q_name, MSG_CHECK_STATUS);
	
	// close /q<PID>
	if (mq_close(q) < 0) ERR("prog1 child mq_close");

	exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) 
{
	if(argc!=2) usage();

	char q0_name[MAXLENGTH], q_name[MAXLENGTH], message[MAXLENGTH];;
    int msgLength;
	pid_t pid;

	mqd_t q0; 
	struct mq_attr attr;
	attr.mq_maxmsg = MAXCAPACITY; 
	attr.mq_msgsize = MAXLENGTH; 

	// open q0
	snprintf(q0_name, MAXLENGTH, "/%s", argv[1]);	
	if ( (q0 = TEMP_FAILURE_RETRY(mq_open(q0_name, O_RDONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1 ) ERR("prog1 mq_open q0");
	
	// receive messages from prog2 and create child processes
	sethandler(sigchld_handler,SIGCHLD);
    while(1)
	{
        if ( (msgLength = TEMP_FAILURE_RETRY(mq_receive(q0, message, MAXLENGTH, NULL))) < 1) ERR("prog1 mq_receive");
		message[msgLength]='\0';
        printf("Message received from %s : \"%s\"\n", q0_name, message);
        
        if ( (pid=fork()) < 0 ) ERR("fork");
        if (0 == pid) child_work(message+9); // TODO: must be modified to accept all message types
	}
	
	// close q0
	if (mq_close(q0) < 0) ERR("prog1 mq_close");
	//if (mq_unlink(q_name) < 0) ERR("mq unlink");

	while(wait(NULL)>0) if (DEBUG) printf("[DEBUG] Prog 1 child terminated.\n");
	
	return EXIT_SUCCESS;
}