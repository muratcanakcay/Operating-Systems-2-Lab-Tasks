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
#include <stdatomic.h>
#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
             exit(EXIT_FAILURE))
#define DEBUG 1

void usage(char * name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "n: number of nodes [100, 10000]\n");
    exit(EXIT_FAILURE);
}

typedef struct node
{
	int value;
	struct node* next;
} node;

typedef struct threadArgs
{
	int n;
	_Atomic volatile node* head;
} threadArgs;

void* AllocatorThread(void* voidData)
{
	printf("AllocatorThread started...\n");
	
	_Atomic volatile node* head = ((threadArgs*)voidData)->head;
	int n = ((threadArgs*)voidData)->n;




	printf("AllocatorThread ending...\n");
	return NULL;
}

void* DeallocatorThread(void* voidData)
{
	printf("DeallocatorThread started...\n");
	
	_Atomic volatile node* head = ((threadArgs*)voidData)->head;
	int n = ((threadArgs*)voidData)->n;





	printf("DeallocatorThread ending...\n");	
	return NULL;
}


int main(int argc, char** argv) 
{
	int n; 
    if (2 != argc) usage(argv[0]);
    
	n = atoi(argv[1]);
	if (n<100   || n>10000) usage(argv[0]);	
	printf("Starting with n=%d...\n", n);
	
    threadArgs tArgs = {n, NULL};
	pthread_t tid[2];

	// Start Allocator Thread
	pthread_create(&tid[0], NULL, &AllocatorThread, &tArgs);
	// Start Deallocator Thread
	pthread_create(&tid[1], NULL, &DeallocatorThread, &tArgs);
		
	
	
	
	
	
	
    for (int i = 0; i < 2; i++)
		if (pthread_join(tid[i], NULL) == 0) fprintf(stderr, "Joined with thread %d\n", i+1);

	printf("Main process exiting.\n");
    return EXIT_SUCCESS;
}