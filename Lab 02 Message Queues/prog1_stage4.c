#define _GNU_SOURCE
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>

#define DEBUG 0
#define MAXLENGTH 100
#define MAXCAPACITY 10
#define MSG_CHECK_STATUS "check status"
#define MSG_REGISTER "register"
#define MSG_STATUS "status"


#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
		     		     exit(EXIT_FAILURE))

typedef unsigned int UINT;
typedef struct timespec timespec_t;

void msleep(UINT milisec) {
    time_t sec= (int)(milisec/1000);
    milisec = milisec - (sec*1000);
    timespec_t req= {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if(nanosleep(&req,&req)) ERR("nanosleep");
}

void usage(void) {
	fprintf(stderr,"USAGE: prog1_stage2 q0_name t\n");
	fprintf(stderr,"q0_name - name of the message queue\n");
	fprintf(stderr,"t - sleep interval\n");
	exit(EXIT_FAILURE);
}

void sigchld_handler(int sig) {
	pid_t pid;	

	for(;;)
    {
		pid=waitpid(0, NULL, WNOHANG);
		if(pid > 0 && DEBUG) puts("[DEBUG] Prog1 child terminated\n");
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

void child_work(pid_t rPid, int t) {
    
	char q_name[MAXLENGTH];

	mqd_t q;
	struct mq_attr attr; 
	attr.mq_maxmsg = MAXCAPACITY; 
	attr.mq_msgsize = MAXLENGTH; 
	
	// open /q<PID>
	snprintf(q_name, MAXLENGTH, "/q%d", rPid);
	if( (q = TEMP_FAILURE_RETRY(mq_open(q_name, O_WRONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1) ERR("prog1 child mq_open q");
	
	while(1)
	{
		// send "check status" message to prog2
		if(TEMP_FAILURE_RETRY(mq_send(q, MSG_CHECK_STATUS, strlen(MSG_CHECK_STATUS), 0))) ERR("prog1 child mq_send");
		printf("Message sent on %s : \"%s\"\n", q_name, MSG_CHECK_STATUS);
		printf("Sleeping for %dms\n", t);
		msleep(t);
	}
	
	// close /q<PID>
	if (mq_close(q) < 0) ERR("prog1 child mq_close");

	exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) {

	if(argc!=3) usage();
	int t = strtol(argv[2], NULL, 10);
	if (t < 100 || t > 2000) usage();

	char q0_name[MAXLENGTH], message[MAXLENGTH];
    char *rMsg;
	int rVal;
	pid_t rPid;
	pid_t pid;
	ssize_t msgLength;

	mqd_t q0; 
	struct mq_attr attr;
	attr.mq_maxmsg = MAXCAPACITY; 
	attr.mq_msgsize = MAXLENGTH; 

	// open q0
	snprintf(q0_name, MAXLENGTH, "/%s", argv[1]);
	if ( (q0 = TEMP_FAILURE_RETRY(mq_open(q0_name, O_RDONLY | O_CREAT, 0600, &attr))) == (mqd_t)-1 ) ERR("prog1 mq_open q0");
	
	sethandler(sigchld_handler,SIGCHLD);
    
	while(1)
	{
        // receive message from prog2
		if ( (msgLength = TEMP_FAILURE_RETRY(mq_receive(q0, message, MAXLENGTH, NULL))) < 1) ERR("prog1 mq_receive");
		message[msgLength]='\0';

		// process message
		rMsg = NULL;
		if (sscanf(message, "%ms %d %d", &rMsg, &rPid, &rVal) < 2) continue; // should I check for ENOMEM?
		
		// register message received -- HOW MUCH ERROR CHECKING REQUIRED FOR MSG?
		if ( strncmp(rMsg, MSG_REGISTER, strlen(MSG_REGISTER)) == 0 )
        {
			printf("Register message received : %d\n", rPid);
			if ( (pid=fork()) < 0 ) ERR("fork");
        	if (0 == pid) child_work(rPid, t);
		}

		// status message received -- HOW MUCH ERROR CHECKING REQUIRED FOR MSG?
		if ( strncmp(rMsg, MSG_STATUS, strlen(MSG_STATUS)) == 0 )
        {
			printf("Status message received from %d : %d\n", rPid, rVal);
		}
		
		free(rMsg);
	}
	
	// close q0
	if (mq_close(q0) < 0) ERR("prog1 mq_close");
	//if (mq_unlink(q_name) < 0) ERR("mq unlink");

	while(wait(NULL)>0) if (DEBUG) printf("[DEBUG] Prog 1 child terminated.\n");
	
	return EXIT_SUCCESS;
}