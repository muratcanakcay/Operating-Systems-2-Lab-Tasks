#define _GNU_SOURCE 
#include <stdio.h>
#include <stdlib.h>
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

#define MAXCON 3
#define BACKLOG 3
volatile sig_atomic_t do_work=1;


void usage(char * name){
	fprintf(stderr,"USAGE: %s socket port\n",name);
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
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	
	if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t))) ERR("setsockopt");
	if(bind(socketfd,(struct sockaddr*) &addr,sizeof(addr)) < 0)  ERR("bind");
	if(listen(socketfd, BACKLOG) < 0) ERR("listen");
	return socketfd;
}

// accept connection
int add_new_client(int sfd){
	int nfd;
    socklen_t size = sizeof(struct sockaddr_in);
    struct sockaddr_in addr;
	if((nfd=TEMP_FAILURE_RETRY(accept(sfd, &addr, &size)))<0) {
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

// pselect
void doServer(int fdT){
	int cfd, cons=0;
	fd_set base_rfds, rfds;
	sigset_t mask, oldmask;
    char data[13] = "hello world\n";
    char full[6] = "full\n";
    int con[MAXCON];
    for (int i = 0; i<MAXCON; i++) con[i] = 0;
	
	// set base_rfds once and use in the loop to reset rfds
	FD_ZERO(&base_rfds);
	FD_SET(fdT, &base_rfds);
	
	// set signal mask for pselect
	sigemptyset (&mask);
	sigaddset (&mask, SIGINT);
	sigprocmask (SIG_BLOCK, &mask, &oldmask);

    //ssize_t x = recv(A_sock, &c, 1, MSG_PEEK);
	
	while(do_work)
    {
		rfds=base_rfds;
		
		if(pselect(fdT+1,&rfds,NULL,NULL,NULL,&oldmask)>0)
        {
			if(cons < 3 && (cfd = add_new_client(fdT)) > 0)
            {
				cons++;
                if(bulk_write(cfd, data, 13) < 0 && errno!=EPIPE) ERR("write:");
			}
            else if (cons == 3)
            {
                // I was trying to check if the received message is zero length which would indicate
                // that client has closed connection. 
                
                
                char c;
                ssize_t x = recv(fdT, &c, 1, MSG_PEEK);
                if (x==0) puts("full");
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
	if(argc!=2) 
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
	doServer(fdT);
	
	if(TEMP_FAILURE_RETRY(close(fdT))<0)ERR("close");
	fprintf(stderr,"Server has terminated.\n");
	return EXIT_SUCCESS;
}