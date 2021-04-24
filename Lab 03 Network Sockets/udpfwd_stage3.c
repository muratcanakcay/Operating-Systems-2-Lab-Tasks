#define _GNU_SOURCE 
#include <sys/select.h>
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
#define HERE puts("**************** HERE ***************")
#define ERR(source) (perror(source),\
        fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
        exit(EXIT_FAILURE))

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
    if (bind(socketfd,(struct sockaddr*) &addr,sizeof(addr)) < 0) ERR("bind");
    if (listen(socketfd, BACKLOG) < 0) ERR("listen");
    return socketfd;
}

// accept connection
int add_new_client(int sfd){
    int nfd;
    socklen_t size = sizeof(struct sockaddr_in);
    struct sockaddr_in addr;
    if( (nfd=TEMP_FAILURE_RETRY(accept(sfd, &addr, &size))) < 0 ) 
    {
        if(EAGAIN==errno||EWOULDBLOCK==errno) return -1;
        ERR("accept");
    }

    return nfd;
}

ssize_t bulk_read(int fd, char *buf, size_t count){
    int c;
    size_t len = 0;
    do
    {
        c = TEMP_FAILURE_RETRY(read(fd, buf, count));
        if (c < 0) return c;
        if (0 == c) return len;
        buf += c;
        len += c;
        count -= c;
    } while(count > 0);

    return len;
}

ssize_t bulk_write(int fd, char *buf, size_t count){
    int c;
    size_t len=0;
    
    do
    {
        c = TEMP_FAILURE_RETRY(write(fd, buf, count));
        if (c < 0) return c;
        buf += c;
        len += c;
        count -= c;
    } while(count > 0);
    
    return len;
}

// pselect
void doServer(int fdT)
{
    int cfd, cons=0;
    fd_set base_rfds, rfds;
    sigset_t mask, oldmask;
    char data[] = "Hello message\n";
    char full[] = "Max no of clients reached. Connection not accepted.\n";
    char buf[10];
    int con[BACKLOG];
    for (int i = 0; i < BACKLOG; i++) con[i] = -1;
    
    // set base_rfds once and use in the loop to reset rfds
    FD_ZERO(&base_rfds);
    FD_SET(fdT, &base_rfds);
    
    // set signal mask for pselect
    sigemptyset (&mask);
    sigaddset (&mask, SIGINT);
    sigprocmask (SIG_BLOCK, &mask, &oldmask);    
    
    while(do_work)
    {
        rfds = base_rfds;
        
        if (pselect(FD_SETSIZE, &rfds, NULL, NULL, NULL, &oldmask) > 0)
        {
            // check for client disconnects
            for (int i = 0; i < BACKLOG; i++)
            {
                // if recv() call returns zero, it means the connection is closed on the other side
                if (FD_ISSET(con[i], &rfds) && recv(con[i], buf, sizeof(buf), MSG_PEEK) == 0) 
                {
                    if (TEMP_FAILURE_RETRY(close(con[i])) < 0) ERR("close");
                    fprintf(stderr, "Client disconnected closing socket. [%d]\n", --cons);
                    FD_CLR(con[i], &base_rfds);
                    con[i] = -1;
                }
            }
            
            //  new client connection
            if (FD_ISSET(fdT, &rfds) && (cfd = add_new_client(fdT)) > 0)
            {
                // if less than 3 clients add new client
                if (cons < 3)
                {
                    int added = 0;
                    for (int i = 0; i < BACKLOG; i++) 
                    {
                        if (con[i] == -1)
                        {
                            con[i] = cfd;
                            FD_SET(cfd, &base_rfds);
                            added = 1;
                            fprintf(stderr, "Client connected. [%d]\n", ++cons);
                            break;
                        }
                    }
                    if (!added) ERR("Connection add error");
                    
                    if(bulk_write(cfd, data, sizeof(data)) < 0 && errno!=EPIPE) ERR("write:");
                }
                else // max. clients reached. don't add new client, send info msg.
                {
                    if (bulk_write(cfd, full, sizeof(full)) < 0 && errno!=EPIPE) ERR("write:");
                    fprintf(stderr, "Client connection request refused.\n");
                    if (TEMP_FAILURE_RETRY(close(cfd)) < 0) ERR("close");
                }                
            }            
        }
        else
        {
            if(EINTR==errno) continue;
            ERR("pselect");
        }
    }

    fprintf(stderr, "SIGINT received.\n");

    // close open sockets
    for (int i = 0; i < BACKLOG; i++) 
    {
        if (con[i] != -1)
        {
            if (TEMP_FAILURE_RETRY(close(con[i])) < 0) ERR("close");
            fprintf(stderr, "Closing socket. [%d]\n", --cons);
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
    
    // make and bind tcp socket
    fdT=bind_tcp_socket(atoi(argv[1]));
    
    // set tcp socket to nonblocking
    new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
    fcntl(fdT, F_SETFL, new_flags);
    
    // accept connections and read data
    doServer(fdT);
    
    if (TEMP_FAILURE_RETRY(close(fdT)) < 0) ERR("close");
    fprintf(stderr, "Closing listening socket.\n");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}
