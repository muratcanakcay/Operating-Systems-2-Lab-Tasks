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

/* not used in stage 5 & 6
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
*/

// 	return spec;
// }

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
    if ( (q = TEMP_FAILURE_RETRY(mq_open(q_name, O_RDONLY | O_NONBLOCK, 0600, &attrq))) == (mqd_t)-1 ) ERR("mq_open q");
    if (DEBUG) printf("[DEBUG] Prog2 with pid %d: opened message queue with name: '%s'\n", getpid(), q_name);

	printf(".");
    fflush(stdout);

    // set initial notification
    sethandler(sig_handler, SIGRTMIN);
    static struct sigevent not;
	not.sigev_notify=SIGEV_SIGNAL;
	not.sigev_signo=SIGRTMIN;
	not.sigev_value.sival_ptr = NULL;
	if (mq_notify(q, &not) < 0) ERR("mq_notify");

    int val = rand()%2;
    // receive message(s) from /q<PID>
            while(1)
            {
                if ( (msgLength = (mq_receive(q, message, MAXLENGTH, NULL))) < 1 )
                {
                    if (errno == EAGAIN) break;
                    ERR("mq_receive");
                }

                message[msgLength]='\0';
                printf("\nMessage received on %s : '%s'\n", q_name, message);

                snprintf (message, MAXLENGTH, "%s %d %d", MSG_STATUS, pid, val);
                if (TEMP_FAILURE_RETRY(mq_send(q0, message, strlen(message), 0))) ERR("mq_send");
                printf("Message sent on %s : '%s'\n", q0_name, message);
            }

    while(1)
	{
		// randomize value
		val = rand()%2;
		printf(".");
		fflush(stdout);
        
        // sleep - wake up at notification or timeout
        msleep(t);

        // if notification triggered
        while (lastSignal == SIGRTMIN)
        {
            // re-set notification
            lastSignal = 0;
            static struct sigevent not;
	        not.sigev_notify=SIGEV_SIGNAL;
            not.sigev_signo=SIGRTMIN;
            not.sigev_value.sival_ptr = NULL;
            if (mq_notify(q, &not)<0) ERR("mq_notify");

		    // receive message(s) from /q<PID>
            while(1)
            {
                if ( (msgLength = TEMP_FAILURE_RETRY(mq_receive(q, message, MAXLENGTH, NULL))) < 1 )
                {
                    if (errno == EAGAIN) break;
                    ERR("mq_receive");
                }

                message[msgLength]='\0';
                printf("\nMessage received on %s : '%s'\n", q_name, message);

                snprintf (message, MAXLENGTH, "%s %d %d", MSG_STATUS, pid, val);
                if (TEMP_FAILURE_RETRY(mq_send(q0, message, strlen(message), 0))) ERR("mq_send");
                printf("Message sent on %s : '%s'\n", q0_name, message);
            }
        }    
	}

    // close q0 and /q<PID>
    if (mq_close(q0)) ERR("mq_close");
	if (mq_close(q)) ERR("mq_close");
	
	return EXIT_SUCCESS;
}