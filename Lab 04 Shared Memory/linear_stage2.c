#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <netdb.h>
#include <fcntl.h>
#define ERR(source) (perror(source),\
		fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
		exit(EXIT_FAILURE))

#define BACKLOG 3
#define MAXTHREADS 20
#define MAXBUF 16
#define HERE puts("***********************")
volatile sig_atomic_t do_work=1 ;

void usage(char * name){
	fprintf(stderr,"USAGE: %s socket port\n",name);
}

typedef struct threadArgs
{
	pthread_t tid;
	int playerNo;
	int fdT;
	int cfd;
} threadArgs;

typedef struct gamedata_t
{
	pthread_t* tid;
	int playerNo;
	int fdT;
	int cfd;
} gamedata_t;


int checkMsg(char* msg) // check if the message from client is valid
{
	if (strcmp(msg, "-21") == 0 ||
		strcmp(msg, "-1") == 0 ||
		strcmp(msg, "0") == 0 ||
		strcmp(msg, "1") == 0 ||
		strcmp(msg, "2") == 0)
		return 0;
	else return -1;
}

void sigint_handler(int sig) {
	do_work=0;
}

int sethandler( void (*f)(int), int sigNo) {
	struct sigaction act;
	memset(&act, 0, sizeof(struct sigaction));
	act.sa_handler = f;
	if (-1==sigaction(sigNo, &act, NULL))
		return -1;
	return 0;
}

// make socket
int make_socket(int domain, int type){
	int sock;
	sock = socket(domain,type,0);
	if(sock < 0) ERR("socket");
	return sock;
}

// bind tcp socket
int bind_tcp_socket(uint16_t port){
	struct sockaddr_in addr;
	int socketfd, t = 1;
	
	socketfd = make_socket(PF_INET,SOCK_STREAM);
	memset(&addr, 0, sizeof(struct sockaddr_in));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t))) ERR("setsockopt");
	if(bind(socketfd,(struct sockaddr*) &addr,sizeof(addr)) < 0)  ERR("bind");
	if(listen(socketfd, BACKLOG) < 0) ERR("listen");
	return socketfd;
}

// accept connection
int add_new_client(int sfd){
	int nfd;
	if((nfd=TEMP_FAILURE_RETRY(accept(sfd,NULL,NULL)))<0) {
		if(EAGAIN==errno||EWOULDBLOCK==errno) return -1;
		ERR("accept");
	}
	return nfd;
}

ssize_t bulk_read(int fd, char *buf, size_t count){
	int c;
	size_t len=0;
	do{
		c=TEMP_FAILURE_RETRY(read(fd,buf,count));
		if(c<0) return c;
		if(0==c) return len;
		buf+=c;
		len+=c;
		count-=c;
	}while(count>0);
	return len ;
}

ssize_t bulk_write(int fd, char *buf, size_t count){
	int c;
	size_t len=0;
	
    do{
		c=TEMP_FAILURE_RETRY(write(fd, buf, count));
		if(c<0) return c;
		buf+=c;
		len+=c;
		count-=c;
	} while(count>0);
	
    return len ;
}

void* playerThread(void* voidData)
{
	threadArgs* tArgs = (threadArgs*)voidData;
	
	HERE;

	fd_set base_rfds, rfds, wfds;
	sigset_t mask, oldmask;
    int ret = 0;
	int playerNo = tArgs->playerNo;
	int fdT= tArgs->fdT;
	int cfd= tArgs->cfd;
	char data[50] = {0};
	char buf[MAXBUF] = "";
	
	// set base_rfds once and use in the loop to reset rfds
	FD_ZERO(&base_rfds);
	FD_SET(cfd, &base_rfds);
	
	// set signal mask for pselect
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);
	
	snprintf(data, 50, "The game has started.\n");
	if(bulk_write(cfd, data, strlen(data)) < 0 && errno!=EPIPE) ERR("write:");

	while(do_work)
    {
		rfds=base_rfds;
		wfds=base_rfds;
		
		if(pselect(FD_SETSIZE, &rfds, &wfds, NULL, NULL, &oldmask) > 0)
        {
			if (FD_ISSET(cfd, &rfds))
			{
				if ((ret = recv(cfd, buf, MAXBUF, 0)) < 0) ERR("recv"); 
                buf[ret-2] = '\0'; // remove endline char
				if (checkMsg(buf) == 0)
					fprintf(stderr, "RECEIVED MESSAGE: \"%s\" with size %d\n", buf, ret);
			}
        }
	}
	sigprocmask (SIG_UNBLOCK, &mask, NULL);
}


// pselect
void doServer(int fdT, int numPlayers, int boardSize){
	int cfd, cons = 0;
	fd_set base_rfds, rfds ;
	sigset_t mask, oldmask;
    int playerNo = 1;
	char data[50] = {0};
	int* arg;

	threadArgs tArgs[MAXTHREADS]; //TODO: allocate dynamically?
	memset(tArgs, 0, sizeof(threadArgs) * MAXTHREADS);

	for(int i = 0; i<numPlayers; i++)
		tArgs[i].fdT = fdT;
	
	// set base_rfds once and use in the loop to reset rfds
	FD_ZERO(&base_rfds);
	FD_SET(fdT, &base_rfds);
	
	// set signal mask for pselect
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);
	
	while(do_work)
    {
		rfds=base_rfds;
		
		if(pselect(FD_SETSIZE, &rfds, NULL, NULL, NULL, &oldmask) > 0)
        {
			//  new client connection
			if((cfd = add_new_client(fdT)) >= 0)  // why >= ??
            {
				snprintf(data, 50, "You are player#%d. Please wait...\n", playerNo);
				if(bulk_write(cfd, data, strlen(data)) < 0 && errno!=EPIPE) ERR("write:");
				//if(TEMP_FAILURE_RETRY(close(cfd))<0)ERR("close");
				tArgs[playerNo-1].cfd = cfd;
				tArgs[playerNo-1].playerNo = playerNo;
				cons++;
				fprintf(stderr, "Added player %d with fd: %d Active connections: %d.\n", tArgs[playerNo-1].playerNo, tArgs[playerNo-1].cfd, cons);
				

				HERE;
				
				if (playerNo == numPlayers)
				{
					for(int i = 0; i < numPlayers; i++)
					{
						printf("Creating player thread no %d\n", i);
						if (pthread_create(&tArgs[i].tid, NULL, playerThread, &tArgs[i])) ERR("pthread_create");
					}
				}

				playerNo++;
			}
            else
            {
                if(EINTR==errno) continue;
                ERR("pselect");
            }
        }
	}
	sigprocmask (SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char** argv) 
{
	int fdT;
	int new_flags;

	if(argc!=4) 
    {
		usage(argv[0]);
		return EXIT_FAILURE;
    }
	

	if(sethandler(SIG_IGN,SIGPIPE)) ERR("Seting SIGPIPE:");
	if(sethandler(sigint_handler,SIGINT)) ERR("Seting SIGINT:");
	
	// make and bind both local and tcp sockets
	fdT=bind_tcp_socket(atoi(argv[1]));
	
	// set both local and tcp sockets to nonblocking
	new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
	fcntl(fdT, F_SETFL, new_flags);
	
	// accept connections and read data
	doServer(fdT, atoi(argv[2]), atoi(argv[3]));
	
	if(TEMP_FAILURE_RETRY(close(fdT))<0)ERR("close");
	fprintf(stderr,"Server has terminated.\n");
	return EXIT_SUCCESS;
}
