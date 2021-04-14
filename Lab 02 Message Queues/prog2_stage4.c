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

#define DEBUG 0
#define MAXLENGTH 100
#define MAXCAPACITY 100
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
	fprintf(stderr,"USAGE: prog2_stage2 q0_name t\n");
	fprintf(stderr,"q0_name - name of the message queue\n");
	fprintf(stderr,"t - sleep interval\n");
	exit(EXIT_FAILURE);
}

timespec_t setTimer(int t)
{
	timespec_t spec;
	clock_gettime(CLOCK_REALTIME, &spec);
	
	spec.tv_nsec += t * 1.0e6;
	
	while(spec.tv_nsec > 1.0e9)
	{
		spec.tv_nsec -= 1.0e9;
		spec.tv_sec++;
	}

	return spec;
}

int main(int argc, char** argv) {

	if(argc!=3) usage();
	int t = strtol(argv[2], NULL, 10);
	if (t < 100 || t > 2000) usage();

	char q0_name[MAXLENGTH], q_name[MAXLENGTH], message[MAXLENGTH];
	int msgLength;
	pid_t pid = getpid();
	srand(time(NULL));
	
	mqd_t q0, q; 										
	struct mq_attr attrq0, attrq; 						
	attrq0.mq_maxmsg = attrq.mq_maxmsg = MAXCAPACITY;
	attrq0.mq_msgsize = attrq.mq_msgsize = MAXLENGTH; 	
	
	// open q0 
	snprintf(q0_name, MAXLENGTH, "/%s", argv[1]);
	if( (q0 = TEMP_FAILURE_RETRY(mq_open(q0_name, O_WRONLY, 0600, &attrq0))) == (mqd_t)-1 ) 
	{
		if (errno == ENOENT) fprintf(stderr, "Error: message queue \"%s\" not found\n", q0_name);
		ERR("mq_open q0");
	}
    
	// send register message to prog1
	snprintf (message, MAXLENGTH, "%s %d", MSG_REGISTER, pid);
	
	if (TEMP_FAILURE_RETRY(mq_send(q0, message, strlen(message), 0))) ERR("mq_send");
	printf("Message sent on %s : '%s'\n", q0_name, message);

    // open /q<PID>
	snprintf (q_name, MAXLENGTH, "/q%d", pid);
    if ( (q = TEMP_FAILURE_RETRY(mq_open(q_name, O_RDONLY, 0600, &attrq))) == (mqd_t)-1 ) ERR("mq_open q");
    if (DEBUG) printf("[DEBUG] Prog2 with pid %d: opened message queue with name: '%s'\n", getpid(), q_name);

	while(1)
	{
		// set timer 
		timespec_t waitTime = setTimer(t);
		
		// wait for message from /q<PID>
		printf(".");
		fflush(stdout);
		if ( (msgLength = TEMP_FAILURE_RETRY(mq_timedreceive(q, message, MAXLENGTH, NULL, &waitTime))) < 1 )
		{
			if (errno == ETIMEDOUT) continue;
			ERR("mq_receive");
		}

		// randomize value
		int val = rand()%2;

		message[msgLength]='\0';
		printf("\nMessage received on %s : '%s'\n", q_name, message);

		snprintf (message, MAXLENGTH, "%s %d %d", MSG_STATUS, pid, val);
		if (TEMP_FAILURE_RETRY(mq_send(q0, message, strlen(message), 0))) ERR("mq_send");
		printf("Message sent on %s : '%s'\n", q0_name, message);
	}

    // close q0 and /q<PID>
    if (mq_close(q0)) ERR("mq_close");
	if (mq_close(q)) ERR("mq_close");
	
	return EXIT_SUCCESS;
}