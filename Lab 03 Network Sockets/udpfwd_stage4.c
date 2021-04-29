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
#include <ctype.h>
#include <stdbool.h>

#define MAX_UDPFWD 10
#define MAX_UDPLISTEN 10
#define MAX_TCP 3
#define MAXBUF 65507
#define BACKLOG 3
#define ERR(source) (perror(source),\
        fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
        exit(EXIT_FAILURE))

volatile sig_atomic_t do_work = 1;

typedef struct udpfwd_t
{
    int fd;
    int fwdCount;
    char* port;
    char* fwdAddr[MAX_UDPFWD];
    char* fwdPort[MAX_UDPFWD];
    struct sockaddr_in fwdList[MAX_UDPFWD];

} udpfwd_t;

void usage(char * name){
    fprintf(stderr,"USAGE: %s port\n", name);
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

// make address
struct sockaddr_in make_address(char *address, char *port){
    int ret;
    struct sockaddr_in addr;
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    if((ret=getaddrinfo(address,port, &hints, &result))){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        exit(EXIT_FAILURE);
    }
    addr = *(struct sockaddr_in *)(result->ai_addr);
    freeaddrinfo(result);
    return addr;
}

// bind internet socket
int bind_inet_socket(uint16_t port, int type){
    struct sockaddr_in addr;
    int socketfd, t = 1;
    
    socketfd = make_socket(PF_INET,type);
    memset(&addr, 0, sizeof(struct sockaddr_in));
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t))) ERR("setsockopt");
    if(bind(socketfd,(struct sockaddr*) &addr,sizeof(addr)) < 0)  ERR("bind");
    if(SOCK_STREAM==type)
        if(listen(socketfd, BACKLOG) < 0) ERR("listen");
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

// THIS FUNCTION IS TOO LONG!!!
int process_fwd(char* token, char* saveptr1, udpfwd_t* udpFwdList, fd_set* base_rfds)
{
    char *subtoken, *subsubtoken, *str, *str2, *saveptr2, *saveptr3;
    char fwdAddr[16], fwdPort[11], udpListen[11];
    int i, j, k, l, udpNo, fwdNo = 0;

    // check if MAX_UDPLISTEN limit is reached
    for (udpNo = 0; udpNo < MAX_UDPLISTEN; udpNo++)
        if (udpFwdList[udpNo].fd == -1) break;
    
    if (udpNo == MAX_UDPLISTEN) 
    {
        fprintf(stderr, "UDP rule limit reached! (max: %d)\n", MAX_UDPLISTEN);
        return -1;
    }

    // get port number to listen at
    token = strtok_r(NULL, " ", &saveptr1);
    
    //check port number is ok
    for (j = 0; j < strlen(token); j++)
    {
        if (isdigit(token[j])) continue;
        fprintf(stderr, "Error in udp listen port number!\n");
        return -1;
    }

    strncpy(udpListen, token, strlen(token));
    fprintf(stderr, "Listen port: %s\n", udpListen);

    for (j = 0; j < MAX_UDPLISTEN; j++)
    {
        if (udpFwdList[j].fd == -1 || atoi(udpListen) != atoi(udpFwdList[j].port)) continue;
        fprintf(stderr, "There's already a rule defined for this port! Close it first.\n");
        return -1;
    }

    // get ip:port to forward to
    while(1)
    {
        if (fwdNo == MAX_UDPFWD) 
        {
            fprintf(stderr, "UDP rule lists too many forward addresses (max:%d)!\n", MAX_UDPFWD);
            return -1;
        }
        
        token = strtok_r(NULL, " ", &saveptr1);
        if (token == NULL) break;

        // ip:port
        fprintf(stderr, "[%d] ip:port = %s\n", ++i, token);

        for (j = 0, str = token; ;j++, str = NULL) 
        {
            subtoken = strtok_r(str, ":", &saveptr2);
            if (subtoken == NULL)
                break;

            // check ip:port has only 2 segments
            if(j >= 2) 
            {
                fprintf(stderr, "Error in ip:port!\n");
                return -1;
            }

            // check port format
            if (j == 1)
            {	
                for (k = 0; k < strlen(subtoken); k++)
                {
                    if (!isdigit(subtoken[k]))
                    {
                        fprintf(stderr, "Error in ip:port - port is not a number!\n");
                        return -1;
                    }
                }

                strncpy(fwdPort, subtoken, strlen(subtoken));
                printf(" PORT --> %s\n", fwdPort);
            }

            //check ip format
            if (j == 0)
            {
                strncpy(fwdAddr, subtoken, strlen(subtoken));
                printf(" IP   --> %s\n", fwdAddr);

                for (k = 0, str2 = subtoken; ;k++, str2 = NULL) 
                {
                    subsubtoken = strtok_r(str2, ".", &saveptr3);
                    if (subsubtoken == NULL)
                        break;
                    
                    // check ip has 4 segments
                    if(k >= 4) 
                    {
                        fprintf(stderr, "Error in ip:port - ip has more than 4 parts!\n");
                        return -1;
                    }

                    // check each segment is a number
                    for (l = 0; l < strlen(subsubtoken); l++)
                    {
                        if (!isdigit(subsubtoken[l]))
                        {
                            fprintf(stderr, "Error in ip:port - part of ip is not a number!\n");
                            return -1;
                        }
                    }

                    // check each segment is < 256
                    
                    if (strtol(subsubtoken, NULL, 10) > 255)
                    {
                        fprintf(stderr, "Error in ip:port - part of ip is greater than 255!\n");
                        return -1;
                    }					

                    printf("      --> %s\n", subsubtoken);
                }

                if(k < 4) 
                {
                    fprintf(stderr, "Error in ip:port - ip has less than 4 parts!\n");
                    return -1;
                }
            }
        }

        if(j < 2) 
        {
            fprintf(stderr, "Error in ip:port - one argument missing!\n");
            return -1;
        }

        // ip:port has no errors, make address add to forwarding list		
        printf("[%d] %s:%s\n", fwdNo, fwdAddr, fwdPort);
        udpFwdList[udpNo].fwdList[fwdNo++] = make_address(fwdAddr, fwdPort);
        udpFwdList[udpNo].port = udpListen;
        udpFwdList[udpNo].fwdCount = fwdNo;
    }

    // open socket and udp and add udp to base_rfds
    int udpFd = bind_inet_socket(atoi(udpListen), SOCK_DGRAM); 
    udpFwdList[udpNo].fd = udpFd;
    FD_SET(udpFwdList[udpNo].fd, base_rfds);

    fprintf(stderr, "fwdCount:%d\n", udpFwdList[udpNo].fwdCount);

    return 0;
}

int process_msg(char* msg, udpfwd_t* udpFwdList, fd_set* base_rfds)
{
    char *token, *saveptr1;
    
    token = strtok_r(msg, " ", &saveptr1);

    if (strcmp(token, "fwd") == 0)
    {
        // stage 4
        if(process_fwd(token, saveptr1, udpFwdList, base_rfds) < -1) return -1;
        return 0;
    }
    else if (strcmp(token, "close") == 0)
    {
        // stage 5
        fprintf(stderr, "CLOSE = %s\n", token);
        return 0;
    }
    else if(strcmp(token, "show") == 0)
    {
        // stage 6
        fprintf(stderr, "SHOW = %s\n", msg);
        return 0;
    }
    else return -1;	
}

// pselect
void doServer(int fdT)
{
    int cfd, ret, tcpCons=0, udpCons = 0, tcpCon[MAX_TCP];
    char buf[MAXBUF];
    char hello[] = "Hello message\n";
    char full[] = "Max no of clients reached. Connection not accepted.\n";
    char msgerror[] = "Unrecognized command.\n";
    fd_set base_rfds, rfds;
    sigset_t mask, oldmask;
    udpfwd_t udpFwdList[MAX_UDPLISTEN];
    
    // set all file descriptors to -1
    for (int i = 0; i < MAX_TCP; i++) tcpCon[i] = -1;
    for (int i = 0; i < MAX_UDPLISTEN; i++) udpFwdList[i].fd = -1;
    
    // set base_rfds once and use in the loop to reset rfds
    FD_ZERO(&base_rfds);
    FD_SET(fdT, &base_rfds);
    
    // set signal mask for pselect
    sigemptyset (&mask);
    sigaddset (&mask, SIGINT);
    sigprocmask (SIG_BLOCK, &mask, &oldmask);

    int fdU = make_socket(PF_INET, SOCK_DGRAM); // udp send socket
    
    while(do_work) // until SIGINT
    {
        rfds = base_rfds;
        
        if (pselect(FD_SETSIZE, &rfds, NULL, NULL, NULL, &oldmask) > 0) // FD_SETSIZE bad??
        {
            // check incoming tcp
            for (int i = 0; i < MAX_TCP; i++)
            {
                memset(buf, 0, sizeof(buf));

                if (FD_ISSET(tcpCon[i], &rfds))
                {
                    // if recv() call returns zero connection is closed on the other side
                    if(recv(tcpCon[i], buf, MAXBUF, MSG_PEEK) == 0) 
                    {
                        fprintf(stderr, "Client disconnected. Closing socket. [%d left]\n", --tcpCons);
                        tcpCon[i] = -1;
                        FD_CLR(tcpCon[i], &base_rfds);
                        if (TEMP_FAILURE_RETRY(close(tcpCon[i])) < 0) ERR("close");
                    }
                    else 
                    {
                        if ((ret = recv(tcpCon[i], buf, MAXBUF, 0)) < 0) ERR("recv"); 
                        buf[ret-2] = '\0'; // remove endline char - WHY -2 ??
                        
                        fprintf(stderr, "RECEIVED: --%s--\n", buf);
                        
                        if (process_msg(buf, udpFwdList, &base_rfds) < 0) // if unrecognized format
                        {
                            if(bulk_write(tcpCon[i], msgerror, sizeof(msgerror)) < 0 && errno!=EPIPE) ERR("write:");
                        }
                    }
                }
            }

            // check incoming udp 
            for (int i = 0; i < MAX_UDPLISTEN; i++)
            {
                memset(buf, 0, sizeof(buf));

                if (FD_ISSET(udpFwdList[i].fd, &rfds))
                {
                    //receive udp message
                    if((ret = recv(udpFwdList[i].fd, buf, MAXBUF, 0)) < 0) ERR("udp read");
                    buf[ret] = '\0';
                    fprintf(stderr, "%s\n", buf);

                    // forward udp message
                    for (int j = 0; j < udpFwdList[i].fwdCount; j++)
                        if(TEMP_FAILURE_RETRY(sendto(fdU, buf, sizeof(buf), 0, &udpFwdList[i].fwdList[j], sizeof(udpFwdList[i].fwdList[j]))) <0 ) ERR("sendto:");
                }
            }
            
            //  new client connection
            if (FD_ISSET(fdT, &rfds) && (cfd = add_new_client(fdT)) > 0)
            {
                // if less than 3 clients add new client
                if (tcpCons < 3)
                {
                    int added = 0;
                    for (int i = 0; i < BACKLOG; i++) 
                    {
                        if (tcpCon[i] == -1)
                        {
                            tcpCon[i] = cfd;
                            FD_SET(cfd, &base_rfds);
                            added = 1;
                            fprintf(stderr, "Client connected. [%d]\n", ++tcpCons);
                            break;
                        }
                    }
                    if (!added) ERR("Connection add error");
                    
                    if(bulk_write(cfd, hello, sizeof(hello)) < 0 && errno!=EPIPE) ERR("write:");
                }
                else // max. clients reached. send info msg.
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
    } // endwhile

    fprintf(stderr, "SIGINT received.\n");

    // close open udp sockets
    if(TEMP_FAILURE_RETRY(close(fdU))<0)ERR("close");
    fprintf(stderr, "Closing udp send socket.\n");

    for (int i = 0; i < MAX_UDPLISTEN; i++) if (udpFwdList[i].fd >= 0) udpCons++;
    for (int i = 0; i < MAX_UDPLISTEN; i++) 
    {
        if (udpFwdList[i].fd < 0) continue;
        if (TEMP_FAILURE_RETRY(close(udpFwdList[i].fd)) < 0) ERR("close");
        fprintf(stderr, "Closing udp listen socket. [%d left]\n", --udpCons);
    }

    // close open tcp sockets
    for (int i = 0; i < MAX_TCP; i++) 
    {
        if (tcpCon[i] < 0) continue;
        if (TEMP_FAILURE_RETRY(close(tcpCon[i])) < 0) ERR("close");
        fprintf(stderr, "Closing tcp send socket. [%d left]\n", --tcpCons);
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
    
    // make and bind tcp listen socket
    fdT=bind_inet_socket(atoi(argv[1]), SOCK_STREAM);
    
    // set tcp listen socket to nonblocking
    new_flags = fcntl(fdT, F_GETFL) | O_NONBLOCK;
    fcntl(fdT, F_SETFL, new_flags);
    
    // accept connections and read data
    doServer(fdT);
    
    // close tcp listen socket
    if (TEMP_FAILURE_RETRY(close(fdT)) < 0) ERR("close");
    fprintf(stderr, "Closing tcp listen socket.\n");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}
