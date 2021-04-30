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

#define MAX_UDPFWD 10       // max forward addresses per rule
#define MAX_UDPLISTEN 10    // max forwarding rules allowed
#define MAX_TCP 3           // max tcp clients allowed to connect
// error codes
#define INVLDCMD -10
#define UDPLIMIT -11
#define INVLDPRT -12
#define DPLCTRUL -13
#define FWDLIMIT -14
#define MAKEADDR -15
#define NOSOCKET -16
#define INVLDIP  -17
// ----------
#define MAXBUF 65507
#define BACKLOG 3
#define ERR(source) (perror(source),\
        fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
        exit(EXIT_FAILURE))

volatile sig_atomic_t do_work = 1;

typedef struct udpfwd_t{
    int fd;
    int fwdCount;                   // no. of forward addreses in this rule
    char port[6];                   // port no. to listen on
    char fwdAddr[MAX_UDPFWD][16]; 	// IPv4 address can be at most 16 chars (including periods)
    char fwdPort[MAX_UDPFWD][6];	// Port number can be at most 65535 = 5 chars
    struct sockaddr_in fwdList[MAX_UDPFWD];

} udpfwd_t;
void usage(char * name){
    fprintf(stderr,"USAGE: %s port\n", name);
}
int isnumeric(char* str){
    for (int i = 0; i < strlen(str); i++)
    {
        if (!isdigit(str[i]))
        {
            return -1;
        }
    }
    return 0;
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
int make_socket(int domain, int type){
    int sock;
    sock = socket(domain,type,0);
    if(sock < 0) ERR("socket");
    return sock;
}
struct sockaddr_in make_address(char *address, char *port, int* err){
    int ret;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    struct addrinfo *result;
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    if((ret=getaddrinfo(address, port, &hints, &result))){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
        *err = -1;
        return addr;
    }
    addr = *(struct sockaddr_in *)(result->ai_addr);
    freeaddrinfo(result);
    return addr;
}
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

// check that the given string is a valid ip address
int validateIp(char* ipAddr){
    int i = 0;
    char *token, *str, *saveptr;
    
    if (ipAddr[0] == '.' || ipAddr[strlen(ipAddr)] == '.') 
    {
        fprintf(stderr, "IP address format wrong.");
        return INVLDIP;
    }
    
    for(i = 1; i < strlen(ipAddr) - 2; i++)
    {
        if (ipAddr[i] != '.' || ipAddr[i+1] != '.') continue;
        fprintf(stderr, "IP address format wrong.");
        return INVLDIP;
    }
    
    for (i = 0, str = ipAddr; ;i++, str = NULL) 
    {
        token = strtok_r(str, ".", &saveptr);
        if (token == NULL)
            break;

        fprintf(stderr, "      --> %s\n", token);
        
        // check ip has at most 4 segments
        if(i >= 4) 
        {
            fprintf(stderr, "Error in ip:port - ip has more than 4 parts!\n");
            return INVLDIP;
        }

        // check each segment is a number
        if(isnumeric(token) < 0) 
        {
            fprintf(stderr, "Error in IP number - part of ip is not a number!\n");   
            return INVLDIP;
        }

        // check each segment is < 256
        if (strtol(token, NULL, 10) > 255)
        {
            fprintf(stderr, "Error in IP number - part of ip is greater than 255!\n");
            return INVLDIP;
        }
    }

    // check ip has at least 4 segments
    if(i < 4)
    {
        fprintf(stderr, "Error in ip:port - ip has less than 4 parts!\n");
        return INVLDIP;
    }

    return 0;
}

// check that the given string is a valid port number
int validatePort(char* portNo){
    //check port is a number
    if (isnumeric(portNo) < 0)
    {
        fprintf(stderr, "Port number should only contain digits!\n");
        return INVLDPRT;
    }

    if (strtol(portNo, NULL, 10) < 1024 || strtol(portNo, NULL, 10) > 65535)
    {
        fprintf(stderr, "Port number must be between 1024 and 65535.\n");
        return INVLDPRT;
    }

    return 0;
}

// check that given <ip:port> is valid
int validateIpPort(char* ipPort, char*fwdAddr, char* fwdPort){
    int i, ret;
    char *token, *str, *saveptr;
    
    for (i = 0, str = ipPort; ;i++, str = NULL) 
    {
        token = strtok_r(str, ":", &saveptr);
        if (token == NULL)
            break;

        // check <ip:port> has only 2 segments
        if(i >= 2) 
        {
            fprintf(stderr, "Error in ip:port!\n");
            return INVLDCMD;
        }

        // check port format
        if (i == 1)
        {	
            strncpy(fwdPort, token, strlen(token));
            fprintf(stderr, " PORT --> %s\n", fwdPort);

            if ((ret = validatePort(token)) < 0) return ret;
        }

        //check ip format
        if (i == 0)
        {
            strncpy(fwdAddr, token, strlen(token));
            fprintf(stderr, "IP   --> %s\n", fwdAddr);

            if ((ret = validateIp(token)) < 0) return ret;
        }
    }

    if (i < 2) 
    {
        fprintf(stderr, "Error in ip:port - one argument missing!\n");
        return INVLDCMD;
    }

    return 0;
}

// process <fwd> messages
int process_fwd(char* token, char* saveptr1, udpfwd_t* udpFwdList, fd_set* base_rfds){
    char fwdAddr[16] = "", fwdPort[6] = "", udpListen[6] = "";
    int i = 0, j = 0, udpNo = 0, fwdNo = 0;

    // MIGHT NOT BE CHECKING THE LAST FD!!!!
    // check if MAX_UDPLISTEN limit is reached 
    for (udpNo = 0; udpNo < MAX_UDPLISTEN; udpNo++)
        if (udpFwdList[udpNo].fd == -1) break;
    
    if (udpNo == MAX_UDPLISTEN) 
    {
        fprintf(stderr, "UDP rule limit reached! (max: %d)\n", MAX_UDPLISTEN);
        return UDPLIMIT;
    }

    // get port number to listen at
    if ((token = strtok_r(NULL, " ", &saveptr1)) == NULL)
    {
        fprintf(stderr, "fwd command parameters missing!\n");
        return INVLDCMD;
    }
     
    //check port number is ok
    if (validatePort(token) < 0) return INVLDPRT;

    strncpy(udpListen, token, strlen(token));
    fprintf(stderr, "Listen port: -%s-\n", udpListen);

    for (j = 0; j < MAX_UDPLISTEN; j++)
    {
        if (udpFwdList[j].fd != -1) fprintf(stderr, "%d, %s \n", j, udpFwdList[j].port);
		if (udpFwdList[j].fd == -1 || strtol(udpListen, NULL, 10) != strtol(udpFwdList[j].port, NULL, 10)) continue;
        fprintf(stderr, "There's already a rule defined for this port! Close it first.\n");
        return DPLCTRUL;
    }

    // get ip:port to forward to
    while(1)
    {
        int err=0, ret=0;
        
        token = strtok_r(NULL, " ", &saveptr1);
        if (token == NULL) break;

        if (fwdNo == MAX_UDPFWD) 
        {
            fprintf(stderr, "UDP rule lists too many forward addresses (max:%d)!\n", MAX_UDPFWD);
            return FWDLIMIT;
        }

        // ip:port
        fprintf(stderr, "[%d] ip:port = %s\n", ++i, token);
        if((ret = validateIpPort(token, fwdAddr, fwdPort) < 0)) return ret;

        // <ip:port> has no errors, make address and add to forwarding list		
        fprintf(stderr, "[%d] %s:%s\n", fwdNo+1, fwdAddr, fwdPort);
        udpFwdList[udpNo].fwdList[fwdNo] = make_address(fwdAddr, fwdPort, &err);
        if(err < 0) 
        {
            fprintf(stderr, "Cannot connect to %s:%s\n", fwdAddr, fwdPort);
            return MAKEADDR;
        }
		strncpy(udpFwdList[udpNo].port, udpListen, 5);
		strncpy(udpFwdList[udpNo].fwdAddr[fwdNo], fwdAddr, 16);
		strncpy(udpFwdList[udpNo].fwdPort[fwdNo], fwdPort, 5) ;
        udpFwdList[udpNo].fwdCount = ++fwdNo;
    }

    // open socket and udp and add udp to base_rfds
    int udpFd = bind_inet_socket(strtol(udpListen, NULL, 10), SOCK_DGRAM); 
    udpFwdList[udpNo].fd = udpFd;
    FD_SET(udpFwdList[udpNo].fd, base_rfds);

    fprintf(stderr, "Forward addresses in the rule: %d\n", udpFwdList[udpNo].fwdCount);

    return 0;
}

// process <close> messages
int process_close(char* token, char* saveptr1, udpfwd_t* udpFwdList, fd_set* base_rfds){
    int j = 0;
    char udpListen[6] = "";

    // get port number to close
    if ((token = strtok_r(NULL, " ", &saveptr1)) == NULL)
    {
        fprintf(stderr, "Port number missing!\n");
        return INVLDCMD;
    }
    
    //check port number is a number
    if (isnumeric(token) < 0)
    {
        fprintf(stderr, "Error in udp listen port number!\n");
        return INVLDPRT;
    }

    strncpy(udpListen, token, strlen(token));
    fprintf(stderr, "Listen port to close: -%s-\n", udpListen);
    
    for (j = 0; j < MAX_UDPLISTEN; j++)
    {
        // skip until finding the position of the port in the array
        if (udpFwdList[j].fd == -1 || strtol(udpListen, NULL, 10) != strtol(udpFwdList[j].port, NULL, 10)) continue;
        
        // remove udp from base_rdfs, close socket and set fd to -1
        FD_CLR(udpFwdList[j].fd, base_rfds);
        if (TEMP_FAILURE_RETRY(close(udpFwdList[j].fd)) < 0) ERR("close");
        udpFwdList[j].fd = -1;
        fprintf(stderr, "Socket closed\n");
        return 0;
    }

    fprintf(stderr, "There's no open udp socket with port number %s\n", udpListen);
    return NOSOCKET;
}

// reply to <show> command
int sendFwdInfo(int cfd, udpfwd_t* udpFwdList){
    char buf[MAXBUF] = "";
    int ruleNo = 0;
                            
    memset(buf, 0, sizeof(buf));
    sprintf(buf, "\nActive forwarding rules are:\n");
    if(bulk_write(cfd, buf, sizeof(buf)) < 0 && errno!=EPIPE) ERR("write:");
    
    for (int j = 0; j < MAX_UDPLISTEN; j++)
    {
        if (udpFwdList[j].fd == -1) continue;

        ruleNo++;
        memset(buf, 0, sizeof(buf));
        sprintf(buf, "[%02d] Port %-5s is forwarded to:\n", ruleNo, udpFwdList[j].port);
        if(bulk_write(cfd, buf, sizeof(buf)) < 0 && errno!=EPIPE) ERR("write:");

        memset(buf, 0, sizeof(buf));
        sprintf(buf, "%24s : %5s\n%24s   %5s\n", "IP Adress", "Port", "----------------", "-----");
        if(bulk_write(cfd, buf, sizeof(buf)) < 0 && errno!=EPIPE) ERR("write:");
        
        for (int k = 0; k < udpFwdList[j].fwdCount; k++)
        {
            memset(buf, 0, sizeof(buf));
            sprintf(buf, "%24s : %5s\n", udpFwdList[j].fwdAddr[k], udpFwdList[j].fwdPort[k]);
            if(bulk_write(cfd, buf, sizeof(buf)) < 0 && errno!=EPIPE) ERR("write:");
        }
        
        memset(buf, 0, sizeof(buf));
        sprintf(buf, "\n");
        if(bulk_write(cfd, buf, sizeof(buf)) < 0 && errno!=EPIPE) ERR("write:");

        return 0;
    }
}

// process messages from clients
int process_msg(char* msg, udpfwd_t* udpFwdList, fd_set* base_rfds, int cfd){
    char *token, *saveptr1;
    
    token = strtok_r(msg, " ", &saveptr1);

    if (strcmp(token, "fwd") == 0)
    {
        // stage 4
        return(process_fwd(token, saveptr1, udpFwdList, base_rfds));        
    }
    else if (strcmp(token, "close") == 0)
    {
        // stage 5
        return(process_close(token, saveptr1, udpFwdList, base_rfds));
    }
    else if(strcmp(token, "show") == 0)
    {
        // stage 6
        return(sendFwdInfo(cfd, udpFwdList));
    }
    else return INVLDCMD;	
}

// server work
void doServer(int fdT){
    int cfd = 0, ret = 0, tcpCons=0, udpCons = 0, tcpCon[MAX_TCP];
    char buf[MAXBUF+1] = "";
    char hello[] = "Hello message\n";
    char full[] = "Max no of clients reached. Connection not accepted.\n";
    char msgerror[] = "Invalid command. Please check and try again.\n";
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
            //  new client connection
            if (FD_ISSET(fdT, &rfds) && (cfd = add_new_client(fdT)) > 0)
            {
                // if less than 3 clients add new client
                if (tcpCons < 3)
                {
                    int added = 0;
                    for (int i = 0; i < MAX_TCP; i++) 
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
                        buf[ret-2] = '\0'; // remove endline char
                        
                        fprintf(stderr, "RECEIVED: --%s-- with size %d\n", buf, ret);
                        
                        //process incoming message
                        if((process_msg(buf, udpFwdList, &base_rfds, tcpCon[i])) < 0) 
                        {
                            // there's an error in message
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

int main(int argc, char** argv){
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
    fdT=bind_inet_socket(strtol(argv[1], NULL, 10), SOCK_STREAM);
    
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