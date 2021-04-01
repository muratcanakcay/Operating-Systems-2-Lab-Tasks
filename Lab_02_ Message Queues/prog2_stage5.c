#include <asm-generic/errno-base.h>
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <mqueue.h>
#include <pthread.h>

#define DEBUG 0
#define MAXLENGTH 100
#define MAXCAPACITY 100
#define MSG_CHECK_STATUS "check status"
#define MSG_REGISTER "register"
#define MSG_STATUS "status"

#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
		     		     exit(EXIT_FAILURE))

volatile sig_atomic_t lastSignal = 0;
typedef unsigned int UINT;
typedef struct timespec timespec_t;
typedef struct {
	pthread_t tid;
	int randVal;
    int t;
} randArgs_t;

void mSleep(UINT milisec) {
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

void setHandler( void (*f)(int), int sigNo) {
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1==sigaction(sigNo, &act, NULL)) ERR("sigaction");
}

void sigHandler(int sig) 
{
	lastSignal = sig;
    printf("Signal received: %d\n", sig);
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

// randomizes the value every t ms
void* randomizer(void* randArgs) 
{
    randArgs_t* args = (randArgs_t*)randArgs;
    
    while(1)
    {
        mSleep(args->t);
        args->randVal = rand() % 2;
        if (DEBUG) printf("[RANDOMIZER] New random value!\n");
    }
}

int main(int argc, char** argv) 
{
    // cmd line arguments
    if(argc!=3) usage();
	int t = strtol(argv[2], NULL, 10);
	if (t < 100 || t > 2000) usage();

	// variables
    srand(time(NULL));
    char q0_name[MAXLENGTH], q_name[MAXLENGTH], message[MAXLENGTH];
	mqd_t q0, q;
    int msgLength;
	struct mq_attr attrq0, attrq;
	attrq0.mq_maxmsg = attrq.mq_maxmsg = MAXCAPACITY;
	attrq0.mq_msgsize = attrq.mq_msgsize = MAXLENGTH;
    randArgs_t randArgs; // for randomizer thread
    randArgs.randVal = 0;
    randArgs.t = t;
	
    // setHandler(sigHandler, SIGINT);

	// open q0 
	snprintf(q0_name, MAXLENGTH, "/%s", argv[1]);
	if( (q0 = TEMP_FAILURE_RETRY(mq_open(q0_name, O_WRONLY, 0600, &attrq0))) == (mqd_t)-1 ) 
	{
		if (errno == ENOENT) fprintf(stderr, "Error: message queue \"%s\" not found\n", q0_name);
		ERR("mq_open q0");
	}
    
	// send register message to prog1
	snprintf (message, MAXLENGTH, "%s %d", MSG_REGISTER, getpid());
	if (TEMP_FAILURE_RETRY(mq_send(q0, message, strlen(message), 0))) ERR("mq_send");
	printf("Message sent on %s : '%s'\n", q0_name, message);

    // open /q<PID>
	snprintf (q_name, MAXLENGTH, "/q%d", getpid());
    if ( (q = TEMP_FAILURE_RETRY(mq_open(q_name, O_RDONLY, 0600, &attrq))) == (mqd_t)-1 ) ERR("mq_open q");
    printf("%d: opened message queue with name: '%s'\n", getpid(), q_name);

    // TODO: join the thread when exiting!!! (stage 6)
    if (pthread_create(&randArgs.tid, NULL, randomizer, &randArgs)) ERR("Couldn't create thread");

    //
    /* Start receiving check status messages and responding to them */
    //

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

		message[msgLength]='\0';
		printf("\nMessage received on %s : '%s'\n", q_name, message);

		snprintf (message, MAXLENGTH, "%s %d %d", MSG_STATUS, getpid(), randArgs.randVal);
		if (TEMP_FAILURE_RETRY(mq_send(q0, message, strlen(message), 1))) ERR("mq_send");
		printf("Message sent on %s : '%s'\n", q0_name, message);
	}

    // close q0 and /q<PID> (for stage 6)
    if (mq_close(q0)) ERR("mq_close");
	if (mq_close(q)) ERR("mq_close");
	
	return EXIT_SUCCESS;
}