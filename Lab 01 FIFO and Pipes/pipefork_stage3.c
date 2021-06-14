#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
             exit(EXIT_FAILURE))

//MAX_BUFF must be in one byte range
#define MAX_BUFF 200

typedef unsigned int UINT;
typedef struct timespec timespec_t;

void create_m(int cpipe, int n, int t);

void usage(char * name)
{
    fprintf(stderr, "USAGE: %s t n r b\n", name);
    fprintf(stderr, "50<=t<=500\n");
	fprintf(stderr, "3<=n<=30 \n");
	fprintf(stderr, "0<=r<=100 \n");
	fprintf(stderr, "1<=b<=PIPE_BUF-6 \n");
    exit(EXIT_FAILURE);
}

void msleep(UINT milisec) 
{
    time_t sec= (int)(milisec/1000);
    milisec = milisec - (sec*1000);
    timespec_t req= {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if(nanosleep(&req,&req)) ERR("nanosleep");
}

int sethandler( void (*f)(int), int sigNo) 
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sigchld_handler(int sig) 
{
    pid_t pid;
    for(;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (pid == 0) return;
        if (pid < 0) 
        {
            if (ECHILD == errno) return;
            ERR("waitpid:");
        }
    }
}

void parent_work(int ppipe) 
{    
    char buf[PIPE_BUF];
    int status;
	sleep(1);
    
    while(1)
    {
        status = TEMP_FAILURE_RETRY(read(ppipe, buf, 1));
        if (status == 0) break;
        if (status < 0) ERR("read ppipe at parent");

        buf[1] = 0; // set end of buffer to zero
        printf("Parent received %s from c\n", buf);
    }    
}

void c_work(int ppipe, int n, int t) 
{
	int cpipe[2], status;
	char buf[PIPE_BUF];
	
	printf("c starting PID:%d\n", getpid());
	
	if (pipe(cpipe)) ERR("pipe");
	
	create_m(cpipe[1], n, t);

	if (TEMP_FAILURE_RETRY(close(cpipe[1]))) ERR("cpipe write end close at c");

	while(1)
	{
		status = read(cpipe[0], buf, 1);
		if (0 == status) break;
		if (status < 0) ERR("read byte from cpipe");

		printf("c with PID %d sending %c to parent\n", getpid(), buf[0]);
		if (TEMP_FAILURE_RETRY(write(ppipe, buf, 1)) < 0) ERR("write to cpipe");	
	}

	if (TEMP_FAILURE_RETRY(close(cpipe[0]))) ERR("ppipe read end close at c");
}

void m_work(int cpipe, int n, int t) 
{
	char buf[PIPE_BUF];
	
	printf("m starting PID:%d PPID:%d\n", getpid(), getppid());

	srand(getpid());
	int i = 0;
	while(i++ < n)
	{
		buf[0] = 'a' + rand() % ('z'-'a');
		printf("[%d] : m with PID %d sending %c to c with PID %d and sleeping %dms\n", i, getpid(), buf[0], getppid(), t);
		if (TEMP_FAILURE_RETRY(write(cpipe, buf, 1)) < 0) ERR("write to cpipe");
		
		msleep(t);
	}
}

void create_m(int cpipe, int n, int t)
{
	switch (fork()) 
	{
		case 0:
			
			m_work(cpipe, n, t);
			
			if (TEMP_FAILURE_RETRY(close(cpipe))) ERR("close cpipe write end at m");
			printf("m exiting PID:%d PPID:%d\n", getpid(), getppid());
			exit(EXIT_SUCCESS);

		case -1: ERR("m fork:");
	}
}

void create_c_and_pipe(int ppipe, int n, int t) 
{
    int c = 2; 
    while (c) 
    {
        switch (fork()) 
        {
            case 0:
                
                c_work(ppipe, n, t);

				while(wait(NULL) > 0);
				if (TEMP_FAILURE_RETRY(close(ppipe))) ERR("close cpipe write end at m");
				printf("c exiting PID:%d\n", getpid());
                exit(EXIT_SUCCESS);

            case -1: ERR("c fork:");
        }

		c--;
    }
}

int main(int argc, char** argv) 
{
    int t, n, r, b, ppipe[2]; 
    if (5 != argc) usage(argv[0]);
    
    t = atoi(argv[1]);
	n = atoi(argv[2]);
	r = atoi(argv[3]);
	b = atoi(argv[4]);
    if (t<50 || t>500) 			usage(argv[0]);
	if (n<3  || n>30) 			usage(argv[0]);
	if (r<0  || r>100) 			usage(argv[0]);
	if (b<1  || b>PIPE_BUF-6) 	usage(argv[0]);

	printf("t=%d, n=%d, r=%d, b=%d\n", t, n, r, b);
    
    if (sethandler(SIG_IGN, SIGINT)) ERR("Setting SIGINT handler");
    if (sethandler(SIG_IGN, SIGPIPE)) ERR("Setting SIGINT handler");
    if (sethandler(sigchld_handler, SIGCHLD)) ERR("Setting parent SIGCHLD:");
    
    if (pipe(ppipe)) ERR("ppipe");

	create_c_and_pipe(ppipe[1], n, t);

	if (TEMP_FAILURE_RETRY(close(ppipe[1]))) ERR("close parent write end");
    
    parent_work(ppipe[0]);
    
	
	if (TEMP_FAILURE_RETRY(close(ppipe[0]))) ERR("close parent read end");
    while(wait(NULL) > 0);
    return EXIT_SUCCESS;
}
