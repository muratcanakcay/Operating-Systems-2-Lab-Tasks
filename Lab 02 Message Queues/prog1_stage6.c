#define _GNU_SOURCE
#include <fcntl.h>
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
#define MAXCAPACITY 5
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
        if (errno == EINTR && lastSignal == SIGINT) return;
        ERR("nanosleep");
    }
}

void usage(void) {
    fprintf(stderr,"USAGE: prog1_stage6 q0_name t\n");
    fprintf(stderr,"q0_name - name of the message queue\n");
    fprintf(stderr,"t - sleep interval\n");
    exit(EXIT_FAILURE);
}

void sigchld_handler(int sig) {
    pid_t pid;	

    for(;;)
    {
        pid=waitpid(0, NULL, WNOHANG);
        if(pid > 0 && DEBUG) puts("Child process terminated in handler");
        if(pid == 0) return;
        if(pid <= 0) 
        {
            if(errno==ECHILD) return;
            ERR("waitpid");
        }
    }
}

void sigHandler(int sig) 
{
    lastSignal = sig;
    if (DEBUG) printf("Signal received: %d\n", sig);
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
    if( (q = TEMP_FAILURE_RETRY(mq_open(q_name, O_WRONLY | O_CREAT | O_NONBLOCK, 0600, &attr))) == (mqd_t)-1) ERR("prog1 child mq_open q");
    
    
    while(1)
    {
        // send "check status" message to prog2
        if(TEMP_FAILURE_RETRY(mq_send(q, MSG_CHECK_STATUS, strlen(MSG_CHECK_STATUS), 0)))
        { 
            if (errno == EAGAIN) // prog2 terminated
            {
                printf("\n%s closed from other side. Terminating process.\n", q_name);
                break;
            }
            else ERR("prog1 child mq_send");
        }

        printf("\nMessage sent on %s : \"%s\"\n", q_name, MSG_CHECK_STATUS);
        msleep(t);
        if (lastSignal == SIGINT) break;
    }
    
    // close and unlink /q<PID>
    if (mq_close(q) < 0) ERR("prog1 child mq_close");
    if (mq_unlink(q_name) < 0) ERR("mq unlink");
    printf("%s closed and unlinked.\n", q_name);

    exit(EXIT_SUCCESS);
}

int main(int argc, char** argv) 
{
    // cmd line arguments
    if(argc!=3) usage();
    int t = strtol(argv[2], NULL, 10);
    if (t < 100 || t > 2000) usage();

    // variables
    char q0_name[MAXLENGTH], message[MAXLENGTH];
    unsigned rPrio;
    int rVal;
    pid_t rPid;
    pid_t pid;
    ssize_t msgLength;
    mqd_t q0;
    struct mq_attr attr;
    attr.mq_maxmsg = MAXCAPACITY;
    attr.mq_msgsize = MAXLENGTH;

    sethandler(sigchld_handler,SIGCHLD);
    sethandler(sigHandler, SIGINT);
    
    // open q0
    snprintf(q0_name, MAXLENGTH, "/%s", argv[1]);
    if ( (q0 = TEMP_FAILURE_RETRY(mq_open(q0_name, O_RDONLY | O_CREAT | O_EXCL, 0600, &attr))) == (mqd_t)-1 ) 
    {
        if (errno == EEXIST) fprintf(stderr, "Error: message queue \"%s\" already exists\n", q0_name);
        ERR("prog1 mq_open q0");
    }
    
    //
    /* Start receiving messages until SIGINT */
    //

    while(1)
    {
        // receive message from prog2
        while (1)
        {
            errno = 0;
            if ( (msgLength = (mq_receive(q0, message, MAXLENGTH, &rPrio))) < 1)
            {
                if (lastSignal == SIGINT) break;
                else if (errno == EINTR) continue;
                else ERR("mq_receive");
            }
            else break;
        }        
        if (lastSignal == SIGINT) break;

        // process message
        message[msgLength]='\0';
        if (sscanf(message, "%*s %d %d", &rPid, &rVal) < 1)
        {
            if (errno == ENOMEM) ERR("sscanf");
            else continue;
        }

        // register message received
        if (rPrio == 0)
        {
            printf("\n-----Register message received : %d\n", rPid);
            if ( (pid = fork()) < 0 ) ERR("fork");
            if (0 == pid) child_work(rPid, t);
        }

        // status message received
        if (rPrio == 1)
        {
            printf("Message received on %s from PID%d : %d\n", q0_name, rPid, rVal);
        }
    }

    puts("\nSIGINT received, exiting...");
    
    // close and unlink q0
    if (mq_close(q0) < 0) ERR("prog1 mq_close");
    if (mq_unlink(q0_name) < 0) ERR("mq unlink");
    printf("q0 closed and unlinked.\n");

    while(wait(NULL)>0) printf("Child process terminated in wait\n");
    
    return EXIT_SUCCESS;
}