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

#define DEBUG 1
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

void msleep(UINT milisec) {
    time_t sec= (int)(milisec/1000);
    milisec = milisec - (sec*1000);
    timespec_t req= {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if(nanosleep(&req,&req))
    { 
        if (errno == EINTR && lastSignal == SIGRTMIN) 
        {
            printf("nanosleep interrupted by SIGRTMN\n");
            return;
        }
        ERR("nanosleep");
    }
}

void usage(void) {
	fprintf(stderr,"USAGE: prog2_stage2 q0_name t\n");
	fprintf(stderr,"q0_name - name of the message queue\n");
	fprintf(stderr,"t - sleep interval\n");
	exit(EXIT_FAILURE);
}

void sethandler( void (*f)(int, siginfo_t*, void*), int sigNo) {
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_sigaction = f;
	act.sa_flags=SA_SIGINFO; // for receiving the siginfo_t struct with he signal
	if (-1==sigaction(sigNo, &act, NULL)) ERR("sigaction");
}

void sig_handler(int sig, siginfo_t *info, void *p) 
{
	lastSignal = sig;
    printf("signal received %d\n", sig);
}

/* ____not used in stage 5 & 6_____

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
*/

void setNotification(mqd_t q, int sig)
{
    static struct sigevent not;
	not.sigev_notify = SIGEV_SIGNAL;
	not.sigev_signo = sig;
	not.sigev_value.sival_ptr = NULL;
	if (mq_notify(q, &not) < 0) ERR("mq_notify");
}

// reads "check status" messages and responds to them
void processMessages(mqd_t q0, char* q0_name, mqd_t q, char* q_name, randArgs_t* randArgs)
{
    char message[MAXLENGTH];
    int msgLength = 0;
    
    while(1)
    {
        if ( (msgLength = (mq_receive(q, message, MAXLENGTH, NULL))) < 1 )
        {
            if (errno == EAGAIN) return;
            ERR("mq_receive");
        }

        message[msgLength]='\0';
        printf("\nMessage received on %s : '%s'\n", q_name, message);

        snprintf (message, MAXLENGTH, "%s %d %d", MSG_STATUS, getpid(), randArgs->randVal);
        if (TEMP_FAILURE_RETRY(mq_send(q0, message, strlen(message), 0))) ERR("mq_send");
        printf("Message sent on %s : '%s'\n", q0_name, message);
    }
}

// randomizes the value every t ms
void* randomizer(void* randArgs) 
{
    randArgs_t* args = (randArgs_t*)randArgs;
    
    while(1)
    {
        msleep(args->t);
        args->randVal = rand() % 2;
    }
}


int main(int argc, char** argv) {

	
    // cmd line arguments
    if(argc!=3) usage();
	int t = strtol(argv[2], NULL, 10);
	if (t < 100 || t > 2000) usage();

	// variables
    srand(time(NULL));
    char q0_name[MAXLENGTH], q_name[MAXLENGTH], message[MAXLENGTH];
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
	snprintf (message, MAXLENGTH, "%s %d", MSG_REGISTER, getpid());
	if (TEMP_FAILURE_RETRY(mq_send(q0, message, strlen(message), 0))) ERR("mq_send");
	printf("Message sent on %s : '%s'\n", q0_name, message);

    // open /q<PID>
	snprintf (q_name, MAXLENGTH, "/q%d", getpid());
    if ( (q = TEMP_FAILURE_RETRY(mq_open(q_name, O_RDONLY | O_NONBLOCK, 0600, &attrq))) == (mqd_t)-1 ) ERR("mq_open q");
    if (DEBUG) printf("[DEBUG] Prog2 with pid %d: opened message queue with name: '%s'\n", getpid(), q_name);

	sethandler(sig_handler, SIGRTMIN);
    randArgs_t randArgs;
    randArgs.randVal = 0;
    randArgs.t = t;

    // TODO: join the thread when exiting!!!
    if (pthread_create(&randArgs.tid, NULL, randomizer, &randArgs)) ERR("Couldn't create thread");

    //
    /* Start receiving check status messages and responding to them */
    //

    printf(".");
    fflush(stdout);

    // set initial notification
    setNotification(q, SIGRTMIN);

    // receive any message(s) from /q<PID> already waiting in queue
    // and send response to q0
    processMessages(q0, q0_name, q, q_name, &randArgs);

    // enter sleep-randomize-receive loop
    while(1)
	{
		printf(".");
		fflush(stdout);
        
        // sleep - wake up at notification or timeout
        msleep(t);

        // if notification triggered
        while (lastSignal == SIGRTMIN)
        {
            // re-set notification
            lastSignal = 0;
            setNotification(q, SIGRTMIN);
            
            // receive any message(s) from /q<PID> already waiting in queue
            // and send response to q0
            processMessages(q0, q0_name, q, q_name, &randArgs);
        }    
	}

    // close q0 and /q<PID>
    if (mq_close(q0)) ERR("mq_close");
	if (mq_close(q)) ERR("mq_close");
	
	return EXIT_SUCCESS;
}