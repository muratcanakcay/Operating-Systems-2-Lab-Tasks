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

void create_m();

void usage(char * name)
{
    fprintf(stderr, "USAGE: %s t n r b\n", name);
    fprintf(stderr, "10<=t<=1000\n");
	fprintf(stderr, "1<=n<=10 \n");
	fprintf(stderr, "0<=r<=100 \n");
	fprintf(stderr, "1<=a<=PIPE_BUF\n");
    fprintf(stderr, "a<=b<=PIPE_BUF\n");
    exit(EXIT_FAILURE);
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

void c_work() 
{
	printf("c starting PID:%d\n", getpid());
	create_m();
}

void m_work() 
{
	printf("m starting PID:%d PPID:%d\n", getpid(), getppid());
	sleep(1);
}

void create_m()
{
	switch (fork()) 
	{
		case 0:
			
			m_work();

			printf("m exiting PID:%d PPID:%d\n", getpid(), getppid());
			exit(EXIT_SUCCESS);

		case -1: ERR("m fork:");
	}
}

void create_c_and_pipes() 
{
    int c = 2;
    while (c) 
    {
        switch (fork()) 
        {
            case 0:
                
                c_work();

				while(wait(NULL) > 0);
				printf("c exiting PID:%d\n", getpid());
                exit(EXIT_SUCCESS);

            case -1: ERR("c fork:");
        }

		c--;
    }
}

int main(int argc, char** argv) 
{
    int t, n, r, a, b; 
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
    
    if (sethandler(SIG_IGN, SIGINT)) ERR("Setting SIGINT handler");
    if (sethandler(SIG_IGN, SIGPIPE)) ERR("Setting SIGINT handler");
    if (sethandler(sigchld_handler, SIGCHLD)) ERR("Setting parent SIGCHLD:");
    
    create_c_and_pipes(); 
    
    while(wait(NULL) > 0);
    return EXIT_SUCCESS;
}
