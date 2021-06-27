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
#define HERE puts("******************");
#define DEBUG 1

typedef unsigned int UINT;
typedef struct timespec timespec_t;

typedef struct node_t
{
	int value;
	struct node_t* next;
} node_t;

typedef struct threadArgs
{
	int n, a, d;
	volatile _Atomic(node_t*)* headPtr;
} threadArgs;

void usage(char * name)
{
    fprintf(stderr, "USAGE: %s n a d\n", name);
    fprintf(stderr, "n: number of nodes [10, 100000]\n");
	fprintf(stderr, "a: sleep time (ms) between each allocation [0, 2000]\n");
	fprintf(stderr, "d: sleep time (ms) between each deallocation [0, 2000]\n");
    exit(EXIT_FAILURE);
}

void msleep(UINT milisec) 
{
    time_t sec= (int)(milisec/1000);
    milisec = milisec - (sec*1000);
    timespec_t req= {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if(nanosleep(&req,&req)) 
	{
		if (errno == EINTR) return;
		else ERR("nanosleep");
	}
}

void* AllocatorThread(void* voidData)
{
	printf("[A]llocatorThread started...\n");
	
	volatile _Atomic(node_t*)* headPtr = ((threadArgs*)voidData)->headPtr;
	int n = ((threadArgs*)voidData)->n;
	int a = ((threadArgs*)voidData)->a;

	// Allocate nodes - array keeps node pointers
	node_t* nodes[n];
	for (int i = 0; i < n; i++) 
	{
		node_t* nodePtr = malloc(sizeof(node_t));
		nodePtr->value = i+1;
		nodePtr->next = NULL;

		nodes[i] = nodePtr;
	}

	// Add nodes to front of the list
	for (int i = 0; i < n; i++)
	{
		nodes[i]->next = *headPtr;

		while (!atomic_compare_exchange_strong(headPtr, &(nodes[i]->next), nodes[i]))
			; // do nothing

		if (DEBUG) printf("[A] Added node with value %d \n", nodes[i]->value);
		msleep(a);
	}

	printf("[A] AllocatorThread ending...\n");
	return NULL;
}

void* DeallocatorThread(void* voidData)
{
	printf("[D]eallocatorThread started...\n");

	int oor = 0;
	node_t* currentHead;
	volatile _Atomic(node_t*)* headPtr = ((threadArgs*)voidData)->headPtr;
	int n = ((threadArgs*)voidData)->n;
	int d = ((threadArgs*)voidData)->d;

	// Remove nodes to front of the list
	for (int i = 0; i < n; i++)
	{
		while ((currentHead = *headPtr) == NULL); // wait for new node to be added
		
		while (!atomic_compare_exchange_strong(headPtr, &currentHead, currentHead->next))
			; // do nothing

		printf("[D] Read %d from list.", currentHead->value);
		if (currentHead->value != i+1) 
		{
			printf(" ********* OUT OF ORDER READ\n");
			oor++;
		}
		else printf("\n");
		
		free(currentHead);
		msleep(d);
	}

	printf("\n[D] Total out of order reads: %d/%d\n\n", oor, n);	
	printf("[D]eallocatorThread ending...\n");	
	return NULL;
}

int main(int argc, char** argv) 
{
	int n, a, d; 
	pthread_t tid[2];
	node_t* head = NULL;
    
	if (4 != argc) usage(argv[0]);
	n = atoi(argv[1]);
	a = atoi(argv[2]);
	d = atoi(argv[3]);
	if (n<10 || n>100000) usage(argv[0]);
	if (a<0  || a>2000) usage(argv[0]);
	if (d<0  || d>2000) usage(argv[0]);
	
	printf("Starting with n=%d nodes a=%d d=%d...\n", n, a, d);
	
	volatile _Atomic(node_t*)* headPtr = (volatile _Atomic(node_t*)*)&head;
    threadArgs tArgs = {n, a, d, headPtr};

	pthread_create(&tid[0], NULL, &AllocatorThread, &tArgs);
	pthread_create(&tid[1], NULL, &DeallocatorThread, &tArgs);
	
	
	if (pthread_join(tid[0], NULL) == 0) fprintf(stderr, "Joined with AllocatorThread.\n");
	if (pthread_join(tid[1], NULL) == 0) fprintf(stderr, "Joined with DeallocatorThread.\n");

	printf("Main process exiting.\n");
    return EXIT_SUCCESS;
}