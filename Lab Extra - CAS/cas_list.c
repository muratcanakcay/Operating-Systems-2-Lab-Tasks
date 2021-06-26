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
#define HERE puts("**************");

typedef unsigned int UINT;
typedef struct timespec timespec_t;

typedef struct node_t
{
	int value;
	struct node_t* next;
} node_t;

typedef struct threadArgs
{
	int n;
	volatile _Atomic(node_t*)* headPtr;
} threadArgs;

void usage(char * name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "n: number of nodes [1, 10000]\n");
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
	printf("AllocatorThread started...\n");
	
	volatile _Atomic(node_t*)* headPtr = ((threadArgs*)voidData)->headPtr;
	int n = ((threadArgs*)voidData)->n;

	

	// Allocate nodes
	node_t* nodes[n];
	for (int i = 0; i < n; i++) 
	{
		node_t* nodePtr = malloc(sizeof(node_t));
		nodePtr->value = i+1;
		nodePtr->next = NULL;

		nodes[i] = nodePtr;
	}

	

	// Nodes array test
	printf("Nodes allocated:\n");
	for (int i = 0; i < n; i++) 
	{
		printf("%d ", nodes[i]->value);
	}
	printf("\n");
	// ----------------------

	// Add nodes to front of the list
	for (int i = 0; i < n; i++)
	{
		HERE
		printf("Adding node with value %d \n", nodes[i]->value);
		HERE

		if (*headPtr == NULL)
		{ 
			printf("Head is null \n");
			*headPtr = (volatile _Atomic (node_t)*)(nodes[i]);
			printf("Assigned node to head. Head value is %d\n", ((node_t*)*headPtr)->value);
			msleep(100);
			continue;
		}
		
		do 
		{
			nodes[i]->next = (node_t*)*headPtr;
			printf("Assigned head to next of node with value %d \n", nodes[i]->value);
			HERE

			printf("head: %p next: %p new: %p\n", *headPtr, nodes[i]->next, nodes[i]);
			
			if (memcmp((node_t*)*headPtr, nodes[i]->next, sizeof **headPtr) == 0) printf("TRUE\n");
			msleep(100);
			
		} while (!atomic_compare_exchange_strong(headPtr, &(nodes[i]->next), nodes[i]));
	}

	HERE

	// Test linked list allocation
	node_t* current = (node_t*)(*headPtr);
	while(current->next)
	{
		printf("%d ", current->value);
		current=current->next;
	}
	// ----------------------

	printf("AllocatorThread ending...\n");
	return NULL;
}

void* DeallocatorThread(void* voidData)
{
	printf("DeallocatorThread started...\n");
	printf("DeallocatorThread ending...\n");	
	return NULL;
}

int main(int argc, char** argv) 
{
	int n; 
    if (2 != argc) usage(argv[0]);
    
	n = atoi(argv[1]);
	if (n<1   || n>10000) usage(argv[0]);	
	printf("Starting with n=%d...\n", n);
	
	node_t* head = NULL;

	volatile _Atomic(node_t*)* headPtr = (volatile _Atomic(node_t*)*)&head;
    threadArgs tArgs = {n, headPtr};
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