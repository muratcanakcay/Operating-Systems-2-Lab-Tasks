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
#include <stdbool.h>
#include <signal.h>
#include <pthread.h>
#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
             exit(EXIT_FAILURE))
#define DEBUG 1



int main(int argc, char** argv) 
{
	
	
    while(wait(NULL) > 0);
    printf("parent process exiting with PID:%d\n",getpid());
    return EXIT_SUCCESS;
}