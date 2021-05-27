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
#define MAXBUF 16
#define HERE puts("***********************")

volatile sig_atomic_t do_work=true ;
volatile sig_atomic_t do_thread=true ;

void usage(char * name)
{
	fprintf(stderr,"USAGE: %s portNo numPlayers boardSize\n", name);
	exit(EXIT_FAILURE);
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
	bool restart;
	int boardSize;
	int deadPlayers;
	int numPlayers;
	int connectedPlayers;
	int fdT;
	int* board;
	int* cfds;
	player_t* players;
	sem_t* boardSem;
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
	do_work=false;
}

void sigusr1_handler(int sig) {
	do_thread=false;
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
        {
			if (pthread_equal(tid, gameData->players[playerNo].tid))
            return playerNo + 1;
		}
    
    return -1;
}

void prepareBoardText(char* boardText, gamedata_t* gameData)
{
	int boardSize = gameData->boardSize;
	int* board = gameData->board;
	
	snprintf(boardText, 50, "|");
	for (int i = 0; i < boardSize - 1; i++) 
	{
		if (board[i] > 0) snprintf(boardText+(2*i)+1, 50-(2*i)-1, "%d", board[i]);
		else snprintf(boardText+(2*i)+1, 50-(2*i)-1, " ");
		snprintf(boardText+(2*i)+2, 50-(2*i)-2, "|");
	}
	if (board[boardSize - 1] > 0) snprintf(boardText+(2*(boardSize-1))+1, 50-(2*(boardSize-1))-1, "%d", board[boardSize-1]);
	else snprintf(boardText+(2*(boardSize-1))+1, 50-(2*(boardSize-1))-1, " ");

	snprintf(boardText+(2*(boardSize-1))+2, 50-(2*(boardSize-1))-2, "|\n");
}

int sendBoard(int playerNo, gamedata_t* gameData)
{
	char boardText[50] = "";
	int cfd = gameData->cfds[playerNo - 1];	
	
	if (TEMP_FAILURE_RETRY(sem_wait(gameData->boardSem)) == -1) 
	{
		if(errno == EINTR);
		else ERR("sem_wait");
	}

	fprintf(stderr, "boardSem locked by Player %d\n", playerNo);

	prepareBoardText(boardText, gameData);
	if(bulk_write(cfd, boardText, strlen(boardText)) < 0 && errno!=EPIPE) ERR("write:");

	if (sem_post(gameData->boardSem) == -1) ERR("sem_post");
	fprintf(stderr, "boardSem unlocked by Player %d\n", playerNo);

	return 0;
}

void outOfBoard(int playerNo, gamedata_t* gameData)
{
	char data[] = "You lost: You stepped out of the board!\n";
	int cfd = gameData->cfds[playerNo - 1];
	gameData->board[gameData->players[playerNo - 1].pos] = 0;
	gameData->deadPlayers++;

	if(bulk_write(cfd, data, strlen(data)) < 0 && errno!=EPIPE) ERR("write:");
	if(TEMP_FAILURE_RETRY(close(cfd))<0)ERR("close");
}

void steppedOn(int playerNo, int deadPlayerCfd)
{
	char data[50] = "";
	snprintf(data, 50, "PLAYER#%d stepped on you!\n", playerNo);
	if(bulk_write(deadPlayerCfd, data, strlen(data)) < 0 && errno!=EPIPE) ERR("write:");
	if(TEMP_FAILURE_RETRY(close(deadPlayerCfd))<0)ERR("close");
}

void reInitializeGame(gamedata_t* gameData)
{
	gameData->deadPlayers = 0;
	gameData->connectedPlayers = 0;
	
	for (int i = 0; i < gameData->numPlayers; i++) gameData->cfds[i] = -1;
	for (int i = 0; i < gameData->boardSize; i++) gameData->board[i] = 0;
	
	for (int i = 0; i < gameData->numPlayers; i++)
		if (gameData->players[i].tid && pthread_join(gameData->players[i].tid, NULL) == 0) fprintf(stderr, "Joined with thread in restart\n");
	
	free (gameData->players);
	player_t* players;
	if ( (players = (player_t*) calloc(gameData->numPlayers, sizeof(player_t))) == NULL ) ERR("players malloc)");
	gameData->players = players;
	
	for(int i = 0; i < gameData->numPlayers; i++)
	{
		gameData->players[i].seed = rand();
		gameData->players[i].pos = -1;
		gameData->players[i].tid = 0;
	}

	gameData->restart = false;
	do_thread = true;
}

void moveOutOfCell(int playerNo, int oldPos, gamedata_t* gameData)
{
	if (TEMP_FAILURE_RETRY(sem_wait(&gameData->cellSems[oldPos])) == -1) 
		{
			if(errno == EINTR); // HANDLE SIGINT!;
			else ERR("sem_wait");
		}
		fprintf(stderr, "cellSem %d locked by Player %d for leaving\n", oldPos, playerNo);

		gameData->board[gameData->players[playerNo - 1].pos] = 0;
		
		if (sem_post(&gameData->cellSems[oldPos]) == -1) ERR("sem_post");
		fprintf(stderr, "cellSem %d unlocked by Player %d after leaving\n", oldPos, playerNo);
}

void moveIntoCell(int playerNo, int newPos, gamedata_t* gameData)
{
	if (TEMP_FAILURE_RETRY(sem_wait(&gameData->cellSems[newPos])) == -1) 
		{
			if(errno == EINTR);
			else ERR("sem_wait");
		}
		fprintf(stderr, "cellSem %d locked by Player %d for entering\n", newPos, playerNo);
		
		if(gameData->board[newPos] != 0) 
		{
			steppedOn(playerNo, gameData->cfds[gameData->board[newPos] - 1]);
			gameData->deadPlayers++;
		}
		gameData->board[newPos] = playerNo;
		gameData->players[playerNo - 1].pos = newPos;

		if (sem_post(&gameData->cellSems[newPos]) == -1) ERR("sem_post");
		fprintf(stderr, "cellSem %d unlocked by Player %d after entering\n", newPos, playerNo);
}

int getLastStandingPlayer(gamedata_t* gameData)
{
	int i;
	for (i = 0; i < gameData->boardSize; i++) 
	{
		if(gameData->board[i] == 0) continue;
		break;
	}
	
	return gameData->cfds[gameData->board[i] - 1];
}

int makeMove(int move, int playerNo, gamedata_t* gameData)
{
	int boardSize = gameData->boardSize;
	int oldPos = gameData->players[playerNo - 1].pos;
	char winMsg[] = "You have won!\n";

	fprintf(stderr, "player %d making move: %d\n", playerNo, move);

	int newPos = oldPos + move;
	if(newPos < 0 || newPos > boardSize -1) 
	{
		fprintf(stderr, "player %d moved to: %d which is out of bounds\n", playerNo, newPos);
		outOfBoard(playerNo, gameData);
	}
	else 
	{
		moveOutOfCell(playerNo, oldPos, gameData);
		moveIntoCell(playerNo, newPos, gameData);
	}

	if (gameData->deadPlayers == gameData->numPlayers - 1)
	{
		int lastStandingPlayer = getLastStandingPlayer(gameData);
		if (bulk_write(lastStandingPlayer, winMsg, strlen(winMsg)) < 0 && errno!=EPIPE) ERR("write:");
		if (TEMP_FAILURE_RETRY(close(lastStandingPlayer))<0) ERR("close");
		gameData->restart = true;
		kill(0, SIGUSR1);
		return -2;
	}

	return 0;
}

int processMsg(char* msg, int playerNo, gamedata_t* gameData) // check if the message from client is valid
{
	if (strcmp(msg, "0") == 0) return sendBoard(playerNo, gameData);
	else if (strcmp(msg, "-2") == 0 ||
			strcmp(msg, "-1") == 0 ||		
			strcmp(msg, "1") == 0 ||
			strcmp(msg, "2") == 0)
		return makeMove(atoi(msg), playerNo, gameData);
	else return -1;
}

void placePlayer(gamedata_t* gameData)
{
	int playerNo = getPlayerNo(gameData);
	UINT seed = gameData->players[playerNo - 1].seed;
	int pos = gameData->players[playerNo - 1].pos;
	sem_t* boardSem = gameData->boardSem;
	int boardSize = gameData->boardSize;
	int* board = gameData->board;
	bool playerNotPlaced = true;

	while(playerNotPlaced)
	{
		if (TEMP_FAILURE_RETRY(sem_trywait(boardSem)) == -1) 
		{
			if(errno == EAGAIN || errno == EINTR) continue;
			else ERR("sem_wait");
		}

		fprintf(stderr, "boardSem locked by Player %d\n", playerNo);

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
				playerNotPlaced = false;
			}
			else fprintf(stderr, "cannot place player %d at position %d which is occupped by player %d\n", playerNo, tryPos, board[tryPos]); 
		}
	}

	fprintf(stderr, "boardSem unlocked by Player %d\n", playerNo);
	if (sem_post(boardSem) == -1) ERR("sem_post");
	
	msleep(100);
	sendBoard(playerNo, gameData);
}

void* playerThread(void* voidData)
{
	gamedata_t* gameData = (gamedata_t*)voidData;

	fd_set base_rfds, rfds, wfds;
	sigset_t mask, oldmask;
    int ret = 0;
	int playerNo = getPlayerNo(gameData);
	int cfd= gameData->cfds[playerNo - 1];
	
	char data[50] = "";
	char buf[MAXBUF] = "";
	
	// set base_rfds once and use in the loop to reset rfds
	FD_ZERO(&base_rfds);
	FD_SET(cfd, &base_rfds);
	
	// set signal mask for pselect
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigaddset (&mask, SIGUSR1);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);
	
	snprintf(data, 50, "The game has started player %d\n", playerNo);
	if(bulk_write(cfd, data, strlen(data)) < 0 && errno!=EPIPE) ERR("write:");

	placePlayer(gameData);

	while(do_thread && do_work)
    {
		rfds=base_rfds;
		wfds=base_rfds;
		
		if(pselect(FD_SETSIZE, &rfds, &wfds, NULL, NULL, &oldmask) > 0)
        {
			if (FD_ISSET(cfd, &rfds))
			{
				if ((ret = recv(cfd, buf, MAXBUF, 0)) < 0) ERR("recv"); 
                buf[ret-2] = '\0'; // remove endline char
				
				if ( (ret = processMsg(buf, playerNo, gameData)) == 0)
					fprintf(stderr, "RECEIVED MESSAGE: \"%s\" from player %d\n", buf, playerNo);
				else if (ret == -2) 
				{
					fprintf(stderr, "Game over player %d ending thread\n", playerNo);
					break;
				}
			}
        }
	}

	fprintf(stderr, "Game over player %d out of dowork loop\n", playerNo);
	
	sigprocmask (SIG_UNBLOCK, &mask, NULL);
	return NULL;
}

void doServer(gamedata_t* gameData){
	int cfd;
	int fdT = gameData->fdT;
	char data[50] = "";
	char gamestarted[] = "Game already started. Wait for the next round!\n";

	fd_set base_rfds, rfds ;
	sigset_t mask, oldmask;
	
	// set base_rfds once and use in the loop to reset rfds
	FD_ZERO(&base_rfds);
	FD_SET(fdT, &base_rfds);
	
	// set signal mask for pselect
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigaddset (&mask, SIGUSR1);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);
	
	while(do_work)
    {
		rfds=base_rfds;
		
		if(pselect(FD_SETSIZE, &rfds, NULL, NULL, NULL, &oldmask) > 0)
        {
			//  new client connection
			if((cfd = add_new_client(fdT)) >= 0)  // why >= ??
            {
				if(gameData->restart)
				{
					reInitializeGame(gameData);
				}
				
				if(gameData->connectedPlayers == gameData->numPlayers)
				{
					if (bulk_write(cfd, gamestarted, sizeof(gamestarted)) < 0 && errno!=EPIPE) ERR("write:");
					if (TEMP_FAILURE_RETRY(close(cfd)) < 0) ERR("close");
					continue;
				} 
				
				gameData->cfds[gameData->connectedPlayers++] = cfd;
				snprintf(data, 50, "You are player#%d. Please wait...\n", gameData->connectedPlayers);
				if(bulk_write(cfd, data, strlen(data)) < 0 && errno!=EPIPE) ERR("write:");
				fprintf(stderr, "Added player %d with fd: %d Active connections: %d.\n", gameData->connectedPlayers, gameData->cfds[gameData->connectedPlayers - 1], gameData->connectedPlayers);
				
				HERE;
				
				if (gameData->connectedPlayers == gameData->numPlayers)
				{
					for(int i = 0; i < gameData->numPlayers; i++)
					{
						printf("Creating player thread no %d\n", i);
						if (pthread_create(&gameData->players[i].tid, NULL, playerThread, gameData)) ERR("pthread_create");
					}
				}
			}
            else
            {
                if(EINTR == errno) continue;
                ERR("pselect");
            }
        }
	}
	sigprocmask (SIG_UNBLOCK, &mask, NULL);
}

void initializeGame(gamedata_t* gameData, int numPlayers, int boardSize, int fdT)
{
	gameData->restart = false;
	gameData->deadPlayers = 0;
	gameData->connectedPlayers = 0;
	gameData->numPlayers = numPlayers;
	gameData->boardSize = boardSize;
	gameData->fdT = fdT;
	
	player_t* players;
	if ( (players = (player_t*) calloc(gameData->numPlayers, sizeof(player_t))) == NULL ) ERR("players malloc)");
	gameData->players = players;
	
	int* board;
	if ( (board = (int*) calloc(gameData->boardSize, sizeof(int))) == NULL ) ERR("board malloc)");
	gameData->board = board;

	int* cfds;
	if ( (cfds = (int*) malloc(gameData->numPlayers * sizeof(int))) == NULL ) ERR("cfds malloc)");
	for (int i = 0; i < gameData->numPlayers; i++) cfds[i] = -1;
	gameData->cfds = cfds;

	sem_t* cellSems;
	if ( (cellSems = (sem_t*) malloc(gameData->boardSize * sizeof(sem_t))) == NULL ) ERR("cellSems malloc)");
	for (int i = 0; i < gameData->boardSize; i++) if (sem_init(&cellSems[i], 0, 1) != 0) ERR("sem_init");;
	gameData->cellSems = cellSems;
	
	sem_t* boardSem;
	if ( (boardSem = (sem_t*) malloc(sizeof(sem_t))) == NULL ) ERR("boardSem malloc)");
	if (sem_init(boardSem, 0, 1) != 0) ERR("sem_init");
	gameData->boardSem = boardSem;

	for(int i = 0; i < gameData->numPlayers; i++)
	{
		gameData->players[i].seed = rand();
		gameData->players[i].pos = -1;
		gameData->players[i].tid = 0;
	}
}

void endGame(gamedata_t* gameData)
{
	for (int i = 0; i < gameData->numPlayers; i++)
		if (gameData->players[i].tid && pthread_join(gameData->players[i].tid, NULL) == 0) fprintf(stderr, "Joined with thread\n");
	
	free(gameData->players);
	free(gameData->board);
	free(gameData->cfds);

	for (int i = 0; i < gameData->boardSize; i++) if (sem_destroy(&gameData->cellSems[i]) != 0) ERR("sem_destroy");
	free(gameData->cellSems);

	sem_destroy(gameData->boardSem);
	free(gameData->boardSem);
}

int main(int argc, char** argv) 
{
	srand(time(NULL));
	struct gamedata_t gameData;
	int new_flags;
	int fdT;

	if (argc!=4 || atoi(argv[2]) < 2 || atoi(argv[2]) > 5 || atoi(argv[3]) < atoi(argv[2]) || atoi(argv[3]) > atoi(argv[2]) * 5)
	{
		usage(argv[0]);
    }

	int portNo = atoi(argv[1]);
	int numPlayers = atoi(argv[2]);
	int boardSize = atoi(argv[3]);
	
	if(sethandler(SIG_IGN,SIGPIPE)) ERR("Seting SIGPIPE:");
	if(sethandler(sigint_handler,SIGINT)) ERR("Seting SIGINT:");
	if(sethandler(sigusr1_handler,SIGUSR1)) ERR("Seting SIGUSR1:");
	
	// make and bind tcp socket in non-blocking mode
	fdT = bind_tcp_socket(portNo);
	new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
	fcntl(fdT, F_SETFL, new_flags);
	
	// initialize gameData
	initializeGame(&gameData, numPlayers, boardSize, fdT);
	
	// start Game
	doServer(&gameData);
	
	if (TEMP_FAILURE_RETRY(close(fdT))<0) ERR("close");
	fprintf(stderr,"Server has terminated.\n");

	endGame(&gameData); // free resources	

	return EXIT_SUCCESS;
}
