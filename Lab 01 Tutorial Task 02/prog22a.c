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

void usage(char * name)
{
	fprintf(stderr, "USAGE: %s n\n", name);
	fprintf(stderr, "0<n<=10 - number of children\n");
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

void sigchld_handler(int sig) {
	pid_t pid;
	for(;;)
	{
		pid = waitpid(0, NULL, WNOHANG);
		if (0 == pid) return;
		if (0 >= pid) 
		{
			if (ECHILD == errno) return;
			ERR("waitpid:");
		}
	}
}

void child_work(int fd, int R) // read fd not used at this stage
{
	srand(getpid());
	char c = 'a' + rand() % ('z'-'a');
	
	if (write(R, &c, 1) < 0) ERR("write to R");
}

void parent_work(int n, int* fds, int R) 
{
	char c;
	int status;
	srand(getpid());
	
	while( (status = read(R, &c, 1)) == 1 ) printf("%c",c);
	if (status < 0) ERR("read from R");
	
	printf("\n");	
}

void create_children_and_pipes(int n, int *fds, int R) 
{
	int tmpfd[2];
	int max = n;

	while (n) 
	{
		if (pipe(tmpfd)) ERR("pipe");
		
		switch (fork()) 
		{
			case 0: // child
				
				// close fds of previous childs
				while (n < max) if (fds[n] && close(fds[n++])) ERR("close");
				free(fds);
				
				if (close(tmpfd[1])) ERR("close"); //close write end for child
				
				child_work(tmpfd[0], R); // send read fd and write R to child
				
				if (close(tmpfd[0])) ERR("close"); // close read fd for child
				if (close(R)) ERR("close"); // close write R for child
				
				exit(EXIT_SUCCESS);

			case -1: ERR("Fork:");
		}

		if (close(tmpfd[0])) ERR("close"); //close read end for parent
		fds[--n] = tmpfd[1]; // put child's read fd into fds so parent can use it to write to
	}
}

int main(int argc, char** argv) 
{
	int n, *fds, R[2];
	if (2 != argc) usage(argv[0]);
	
	n = atoi(argv[1]);
	if (n<=0 || n>10) usage(argv[0]);
	
	if (pipe(R)) ERR("pipe");
	
	if ( NULL == (fds = (int*)malloc(sizeof(int)*n)) ) ERR("malloc"); // allocate memory for pipe fds
	
	if (sethandler(sigchld_handler, SIGCHLD)) ERR("Seting parent SIGCHLD:");
	
	create_children_and_pipes(n, fds, R[1]); // fds for children to listen, R to write
	
	if (close(R[1])) ERR("close"); // parent closes write end of R
	
	parent_work(n, fds, R[0]); // fds for parent to write, R[0] to listen
	
	while(n--) if (fds[n] && close(fds[n])) ERR("fds close");
	if (close(R[0])) ERR("R close"); // close read R[0] for parent
	
	free(fds);
	return EXIT_SUCCESS;
}