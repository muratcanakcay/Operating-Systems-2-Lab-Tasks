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
#include <time.h>
#include <semaphore.h>
#include <stdbool.h>
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

typedef unsigned int UINT;

typedef struct player_t
{
	pthread_t tid;
	int pos;
	UINT seed;
} player_t;

typedef struct gamedata_t
{
	int boardSize;
	int numPlayers;
	int fdT;
	int* board;
	int* cfds;
	player_t* players;
	sem_t* initSem;
	sem_t* readSem;
	sem_t* cellSems;
} gamedata_t;

typedef struct timespec timespec_t;

//declaration
ssize_t bulk_write(int fd, char *buf, size_t count);

void msleep(UINT milisec) 
{
    time_t sec= (int)(milisec/1000);
    milisec = milisec - (sec*1000);
    timespec_t req= {0};
    req.tv_sec = sec;
    req.tv_nsec = milisec * 1000000L;
    if(nanosleep(&req,&req)) ERR("nanosleep");
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

int getPlayerNo(gamedata_t* gameData)
{    
    pthread_t tid = pthread_self();

    for (int playerNo = 0; playerNo < gameData->numPlayers; playerNo++)
        if (pthread_equal(tid, gameData->players[playerNo].tid))
            return playerNo + 1;
    
    return -1;
}

int sendBoard(gamedata_t* gameData)
{
	char data[50] = "";
	int boardSize = gameData->boardSize;
	int* board = gameData->board;
	int cfd = gameData->cfds[getPlayerNo(gameData) - 1];
	
	// PUT READ SEMAPHORE HERE!!!!


	snprintf(data, 50, "|");
	for (int i = 0; i < boardSize - 1; i++) 
	{
		if (board[i] > 0) snprintf(data+(2*i)+1, 50-(2*i)-1, "%d", board[i]);
		else snprintf(data+(2*i)+1, 50-(2*i)-1, " ");
		snprintf(data+(2*i)+2, 50-(2*i)-2, "|");
	}
	if (board[boardSize - 1] > 0) snprintf(data+(2*(boardSize-1))+1, 50-(2*(boardSize-1))-1, "%d", board[boardSize-1]);
	else snprintf(data+(2*(boardSize-1))+1, 50-(2*(boardSize-1))-1, " ");

	snprintf(data+(2*(boardSize-1))+2, 50-(2*(boardSize-1))-2, "|\n");
	
	if(bulk_write(cfd, data, strlen(data)) < 0 && errno!=EPIPE) ERR("write:");

	return 0;
}

void outOfBoard(int playerNo, gamedata_t* gameData)
{
	char data[] = "You lost: You stepped out of the board!\n";
	int cfd = gameData->cfds[playerNo - 1];
	gameData->board[gameData->players[playerNo - 1].pos] = 0;

	if(bulk_write(cfd, data, strlen(data)) < 0 && errno!=EPIPE) ERR("write:");
	if(TEMP_FAILURE_RETRY(close(cfd))<0)ERR("close");
	
}

int makeMove(int move, int playerNo, gamedata_t* gameData)
{
	int boardSize = gameData->boardSize;
	int* board = gameData->board;
	int cfd = gameData->cfds[playerNo - 1];

	fprintf(stderr, "player %d making move: %d\n", playerNo, move);

	int newPos = gameData->players[playerNo - 1].pos + move;
	if(newPos < 0 || newPos > boardSize -1) 
	{
		fprintf(stderr, "player %d moved to: %d whic is invalid\n", playerNo, newPos);
		outOfBoard(playerNo, gameData);
	}

	gameData->board[gameData->players[playerNo - 1].pos] = 0;
	gameData->players[playerNo - 1].pos = newPos;
	gameData->board[newPos] = playerNo;

	return 0;
}


int processMsg(char* msg, int playerNo, gamedata_t* gameData) // check if the message from client is valid
{
	if (strcmp(msg, "0") == 0) return sendBoard(gameData);
	else if (strcmp(msg, "-2") == 0 ||
			strcmp(msg, "-1") == 0 ||		
			strcmp(msg, "1") == 0 ||
			strcmp(msg, "2") == 0)
		return makeMove(atoi(msg), playerNo, gameData);
	else return -1;
}

void* playerThread(void* voidData)
{
	gamedata_t* gameData = (gamedata_t*)voidData;
	
	HERE;

	fd_set base_rfds, rfds, wfds;
	sigset_t mask, oldmask;
    int ret = 0;
	int playerNo = getPlayerNo(gameData);
	int cfd= gameData->cfds[playerNo - 1];
	int pos = gameData->players[playerNo - 1].pos;
	UINT seed = gameData->players[playerNo - 1].seed;
	int boardSize = gameData->boardSize;
	int* board = gameData->board;
	sem_t* initSem = gameData->initSem;
	char data[50] = {0};
	char buf[MAXBUF] = "";
	
	// set base_rfds once and use in the loop to reset rfds
	FD_ZERO(&base_rfds);
	FD_SET(cfd, &base_rfds);
	
	// set signal mask for pselect
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);
	
	snprintf(data, 50, "The game has started player %d\n", playerNo);
	if(bulk_write(cfd, data, strlen(data)) < 0 && errno!=EPIPE) ERR("write:");

	bool placed = false;
	while(!placed)
	{
		if (TEMP_FAILURE_RETRY(sem_trywait(initSem)) == -1) 
		{
			if(errno == EAGAIN || errno == EINTR) continue;
			else ERR("sem_wait");
		}

		fprintf(stderr, "initSem locked by Player %d\n", playerNo);

		while (pos == -1) 
		{
			int tryPos = rand_r(&seed) % boardSize;

			fprintf(stderr, "trying to place player %d at position %d\n", playerNo, tryPos);

			if(board[tryPos] == 0) 
			{
				board[tryPos] = playerNo;
				pos = tryPos;
				gameData->players[playerNo - 1].pos = pos;

				fprintf(stderr, "player %d placed at position %d\n", playerNo, tryPos);
				placed = true;
			}
			else fprintf(stderr, "cannot place player %d at position %d which is occupped by player %d\n", playerNo, tryPos, board[tryPos]); 
		}
	}

	fprintf(stderr, "initSem unlocked by Player %d\n", playerNo);
	if (sem_post(initSem) == -1) ERR("sem_post");

	//printBoard(board, boardSize);
	
	msleep(100);
	sendBoard(gameData);

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
				if (processMsg(buf, playerNo, gameData) == 0)
				{
					fprintf(stderr, "RECEIVED MESSAGE: \"%s\" with size %d\n", buf, ret);
					//processMsg(buf, gameData, playerNo)
				}
			}
        }
	}
	sigprocmask (SIG_UNBLOCK, &mask, NULL);

	return NULL;
}


// pselect
void doServer(gamedata_t* gameData){
	int cfd, cons = 0;
	
	int fdT = gameData->fdT;
	
	fd_set base_rfds, rfds ;
	sigset_t mask, oldmask;
    int playerNo = 1;
	char data[50] = {0};

	
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
				gameData->cfds[playerNo - 1] = cfd;
				cons++;
				fprintf(stderr, "Added player %d with fd: %d Active connections: %d.\n", playerNo, gameData->cfds[playerNo-1], cons);
				
				HERE;
				
				if (playerNo == gameData->numPlayers)
				{
					for(int i = 0; i < gameData->numPlayers; i++)
					{
						printf("Creating player thread no %d\n", i);
						if (pthread_create(&gameData->players[i].tid, NULL, playerThread, gameData)) ERR("pthread_create");
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
	srand(time(NULL));
	struct gamedata_t gameData;
	
	int fdT;
	int new_flags;

	if(argc!=4) 
    {
		usage(argv[0]);
		return EXIT_FAILURE;
    }

	int portNo = atoi(argv[1]);
	int numPlayers = atoi(argv[2]);
	int boardSize = atoi(argv[3]);

	if (numPlayers < 2 || numPlayers> 5 || boardSize < numPlayers || boardSize > numPlayers * 5)
	{
		usage(argv[0]);
		return EXIT_FAILURE;
    }
	
	if(sethandler(SIG_IGN,SIGPIPE)) ERR("Seting SIGPIPE:");
	if(sethandler(sigint_handler,SIGINT)) ERR("Seting SIGINT:");
	
	// make and bind both local and tcp sockets
	fdT = bind_tcp_socket(portNo);
	
	// set both local and tcp sockets to nonblocking
	new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
	fcntl(fdT, F_SETFL, new_flags);
	
	gameData.numPlayers = numPlayers;
	gameData.boardSize = boardSize;
	gameData.fdT = fdT;

	// init
	
	player_t* players;
	if ( (players = (player_t*) calloc(gameData.numPlayers, sizeof(player_t))) == NULL ) ERR("players malloc)");
	
	int* board;
	if ( (board = (int*) calloc(gameData.boardSize, sizeof(int))) == NULL ) ERR("board malloc)");

	int* cfds;
	if ( (cfds = (int*) malloc(gameData.numPlayers * sizeof(int))) == NULL ) ERR("board malloc)");
	for (int i = 0; i < gameData.numPlayers; i++) cfds[i] = -1;

	sem_t* cellSems;
	if ( (cellSems = (sem_t*) malloc(gameData.boardSize * sizeof(sem_t))) == NULL ) ERR("board malloc)");
	for (int i = 0; i < gameData.boardSize; i++) if (sem_init(&cellSems[i], 0, 1) != 0) ERR("sem_init");;
	
	sem_t initSem;
	if (sem_init(&initSem, 0, 1) != 0) ERR("sem_init");
	
	sem_t readSem;
	if (sem_init(&readSem, 0, 1) != 0) ERR("sem_init");

	gameData.board = board;
	gameData.initSem = &initSem;
	gameData.readSem = &readSem;
	gameData.cellSems = cellSems;
	gameData.players = players;
	gameData.cfds = cfds;
	
	for(int i = 0; i < gameData.numPlayers; i++)
	{
		gameData.players[i].seed = rand();
		gameData.players[i].pos = -1;
	}
	
	// accept connections and read data
	doServer(&gameData);
	
	if (TEMP_FAILURE_RETRY(close(fdT))<0) ERR("close");
	fprintf(stderr,"Server has terminated.\n");
	return EXIT_SUCCESS;
}
