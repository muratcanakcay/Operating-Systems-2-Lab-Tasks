#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
             exit(EXIT_FAILURE))

typedef unsigned int UINT;
typedef struct timespec timespec_t;

void create_m(int c, int n, int t);

void usage(char * name)
{
    fprintf(stderr, "USAGE: %s t n r b a\n", name);
    fprintf(stderr, "10<=t<=1000\n");
	fprintf(stderr, "1<=n<=10 \n");
	fprintf(stderr, "0<=r<=100 \n");
	fprintf(stderr, "1<=a<=PIPE_BUF\n");
    fprintf(stderr, "a<=b<=PIPE_BUF\n");
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

void parent_work() 
{    
    char buf[PIPE_BUF] = "";
    int pfifo, status;
	msleep(200);

    // open pfifofile for reading at parent
    if ((pfifo = open("pfifofile", O_RDONLY))<0) ERR("pfifofile open at parent");
    
    while(1)
    {
        status = TEMP_FAILURE_RETRY(read(pfifo, buf, 1));
        if (status == 0) break;
        if (status < 0) ERR("read ppipe at parent");
        
        printf("\n************Parent received %c from c\n\n", buf[0]);
    }

    if (close(pfifo)<0) perror("pfifo read end close at parent:");    
}

void c_work(int c, int n, int t) 
{
	int pfifo, cfifo, status;
	char buf[PIPE_BUF] = "";
	
	printf("c%d starting PID:%d\n", c, getpid());
	
    if ((pfifo=open("pfifofile", O_WRONLY))<0) ERR("pfifofile open for c");
	
    if (c == 1)
    {
        if ( mkfifo("cfifo1file", S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) < 0 )
		    if (errno!=EEXIST) ERR("create cfifo1file");

        create_m(1, n, t);
        
        if ((cfifo=open("cfifo1file", O_RDONLY))<0) ERR("cfifo1 open for c");
    }
    else 
    {
        if ( mkfifo("cfifo2file", S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) < 0 )
		    if (errno!=EEXIST) ERR("create cfifo2file");

        create_m(2, n, t);

        if ((cfifo=open("cfifo2file", O_RDONLY))<0) ERR("cfifo2 open for c");
    }

	while(1)
	{
		status = read(cfifo, buf, 1);
		if (0 == status) break;
		if (status < 0) ERR("read byte from cpipe");

		printf("c%d with PID %d sending %c to parent\n", c, getpid(), buf[0]);
		if (TEMP_FAILURE_RETRY(write(pfifo, buf, 1)) < 0) ERR("write to pfifo");	
	}

	if (TEMP_FAILURE_RETRY(close(pfifo))) ERR("pfifo write end close at c");
    if (TEMP_FAILURE_RETRY(close(cfifo))) ERR("cfifo read end close at c");
}

void m_work(int c, int n, int t)
{
	int cfifo;
    char buf[PIPE_BUF];

	printf("m%d starting PID:%d PPID:%d\n", c, getpid(), getppid());

    if (c == 1)
    {
        if ((cfifo=open("cfifo1file", O_WRONLY))<0) ERR("cfifo1 open for m");
    }
    else 
    {
        if ((cfifo=open("cfifo2file", O_WRONLY))<0) ERR("cfifo2 open for m");
    }

	srand(getpid());
    int i = 0;
	while(i++ < n)
	{
        buf[0] = 'a' + rand() % ('z'-'a');
        printf("[n=%d] m%d with PID %d sending %c to c with PID %d\n", i, c, getpid(), buf[0], getppid());
        if (TEMP_FAILURE_RETRY(write(cfifo, buf, 1)) < 0) ERR("write to cpipe");

        msleep(t);
    }
    
    if (TEMP_FAILURE_RETRY(close(cfifo))) ERR("cfifo write end close at m");
}

void create_m(int c, int n, int t)
{
	switch (fork()) 
	{
		case 0:
			
			m_work(c, n, t);
			
			printf("m%d exiting PID:%d PPID:%d\n", c, getpid(), getppid());
			exit(EXIT_SUCCESS);

		case -1: ERR("m fork:");
	}
}

void create_c_and_pipe(int n, int t) 
{
    int c = 2; 
    while (c) 
    {
        switch (fork()) 
        {
            case 0:
                
                c_work(c, n, t);

				while(wait(NULL) > 0);
				printf("c%d exiting PID:%d\n", c, getpid());
                exit(EXIT_SUCCESS);

            case -1: ERR("c fork:");
        }

		c--;
    }
}

int main(int argc, char** argv) 
{
    int t, n, r, b, a; 
    if (6 != argc) usage(argv[0]);
    
    t = atoi(argv[1]);
	n = atoi(argv[2]);
	r = atoi(argv[3]);
	a = atoi(argv[4]);
    b = atoi(argv[5]);
    if (t<100 || t>1000) 		usage(argv[0]);
	if (n<1   || n>10) 			usage(argv[0]);
	if (r<0   || r>100) 		usage(argv[0]);
	if (a<1   || a>PIPE_BUF) 	usage(argv[0]);
    if (b<a   || b>PIPE_BUF) 	usage(argv[0]);

	printf("t=%d, n=%d, r=%d, a=%d b=%d\n", t, n, r, a, b);
    
    printf("parent process starting with PID:%d\n",getpid());    
    //if (sethandler(SIG_IGN, SIGINT)) ERR("Setting SIGINT handler");
    if (sethandler(SIG_IGN, SIGPIPE)) ERR("Setting SIGINT handler");
    if (sethandler(sigchld_handler, SIGCHLD)) ERR("Setting parent SIGCHLD:");
    
    if ( mkfifo("pfifofile", S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP) < 0 )
		if (errno!=EEXIST) ERR("create pfifo");
	
	create_c_and_pipe(n, t);
    parent_work();
    
    if (unlink("pfifofile") < 0)ERR("remove pfifofile:");
    if (unlink("cfifo1file") < 0)ERR("remove cfifo1file:");
    if (unlink("cfifo2file") < 0)ERR("remove cfifo2file:");
    
    while(wait(NULL) > 0);
    printf("parent process exiting with PID:%d\n",getpid());
    return EXIT_SUCCESS;
}
