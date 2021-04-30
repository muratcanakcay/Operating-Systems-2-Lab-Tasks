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

#define DEBUG 1            // show/hide debug messages on server

#define MAX_UDPFWD 2       // max forward addresses per rule
#define MAX_UDPLISTEN 2    // max forwarding rules allowed
#define MAX_TCP 3          // max tcp clients allowed to connect
// error codes
#define INVLDCMD -10
#define UDPLIMIT -11
#define INVLDPRT -12
#define DPLCTRUL -13
#define FWDLIMIT -14
#define MAKEADDR -15
#define NOSOCKET -16
#define INVLDIP  -17
// client messages
#define invldcmd "Invalid command. Please check and try again.\n"
#define udplimit "Max. no. of rules reached. Close a connection to add a new rule."
#define invldprt "Port number is not valid. Please check and try again.\n"
#define dplctrul "A forwarding rule for this port already exists. Close it first.\n"
#define fwdlimit "Too many forwarding addresses given."
#define makeaddr "Cannot connect to ip address. Please check and try again.\n"
#define nosocket "There's no rule defined for the given port.\n"
#define invldip  "Invalid ip address format. Please check and try again.\n"
#define sockopen "Rule created.\n"
#define sckclose "Rule deleted.\n"
#define hellomsg "Hello message\n"
#define tcpfull  "Max no of clients reached. Connection not accepted."
// ----------
#define MAXBUF 65507
#define BACKLOG 3
#define ERR(source) (perror(source),\
        fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
        exit(EXIT_FAILURE))

volatile sig_atomic_t do_work = 1;

// info for each udp forwarding rule
typedef struct udpfwd_t{
    int fd;                         // fd of the connection
    int fwdCount;                   // no. of forward addreses in this rule
    char port[6];                   // port to listen on
    char fwdAddr[MAX_UDPFWD][16]; 	// IPv4 address can be at most 15 chars (including periods)
    char fwdPort[MAX_UDPFWD][6];	// port number can be at most 65535 = 5 chars
    struct sockaddr_in fwdList[MAX_UDPFWD];

} udpfwd_t;
void usage(char * name){
    fprintf(stderr,"USAGE: %s port\n", name);
}
int isNumeric(char* str){
    for (int i = 0; i < strlen(str); i++)
    {
        if (!isdigit(str[i]))
        {
            return -1;
        }
    }
    return 0;
}
// returns max of the open file descriptors
int getMaxFd(udpfwd_t* udpFwdList, int* tcpFd, int sfd){
    int i, maxFd = sfd;

    for (i = 0; i < MAX_TCP; i++)
    {
        if (tcpFd[i] > maxFd) maxFd = tcpFd[i];
    }

    for (i = 0; i < MAX_UDPLISTEN; i++)
    {
        if (udpFwdList[i].fd > maxFd) maxFd = udpFwdList[i].fd;
    }

    return maxFd;
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
    
    // check if it starts or ends with period
    if (ipAddr[0] == '.' || ipAddr[strlen(ipAddr)] == '.') 
    {
        if (DEBUG) fprintf(stderr, "IP address format wrong.");
        return INVLDIP;
    }
    
    // check if it contains two adjacent periods
    for(i = 1; i < strlen(ipAddr) - 2; i++)
    {
        if (ipAddr[i] != '.' || ipAddr[i+1] != '.') continue;
        if (DEBUG) fprintf(stderr, "IP address format wrong.");
        return INVLDIP;
    }
    
    // check the segments (this part can be reduced to very small in size by removing debug messages)
    for (i = 0, str = ipAddr; ;i++, str = NULL) 
    {
        token = strtok_r(str, ".", &saveptr);
        if (token == NULL)
            break;

        if (DEBUG) fprintf(stderr, "      --> %s\n", token);
        
        // check ip has at most 4 segments
        if (i >= 4) 
        {
            if (DEBUG) fprintf(stderr, "Error in ip:port - ip has more than 4 parts!\n");
            return INVLDIP;
        }

        // check each segment is a number
        if (isNumeric(token) < 0) 
        {
            if (DEBUG) fprintf(stderr, "Error in IP number - part of ip is not a number!\n");   
            return INVLDIP;
        }

        // check each segment is < 256
        if (strtol(token, NULL, 10) > 255)
        {
            if (DEBUG) fprintf(stderr, "Error in IP number - part of ip is greater than 255!\n");
            return INVLDIP;
        }
    }

    // check ip has at least 4 segments
    if (i < 4)
    {
        if (DEBUG) fprintf(stderr, "Error in ip:port - ip has less than 4 parts!\n");
        return INVLDIP;
    }

    return 0;
}

// check that the given string is a valid port number
int validatePort(char* portNo, bool listenPort){
    //check port is a number
    if (isNumeric(portNo) < 0)
    {
        if (DEBUG) fprintf(stderr, "Port number should only contain digits!\n");
        return INVLDPRT;
    }

    // listen port must be > 1024
    if ( (listenPort && (strtol(portNo, NULL, 10) < 1024)) || strtol(portNo, NULL, 10) > 65535)
    {
        if (DEBUG) fprintf(stderr, "Port number must be between 1024 and 65535.\n");
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
            if (DEBUG) fprintf(stderr, "Error in ip:port!\n");
            return INVLDCMD;
        }

        // check port format
        if (i == 1)
        {	
            strncpy(fwdPort, token, strlen(token));
            if (DEBUG) fprintf(stderr, " PORT --> %s\n", fwdPort);

            if ((ret = validatePort(token, 0)) < 0) return ret;
        }

        //check ip format
        if (i == 0)
        {
            strncpy(fwdAddr, token, strlen(token));
            if (DEBUG) fprintf(stderr, " IP   --> %s\n", fwdAddr);

            if ((ret = validateIp(token)) < 0) return ret;
        }
    }

    if (i < 2) 
    {
        if (DEBUG) fprintf(stderr, "Error in ip:port - one argument missing!\n");
        return INVLDCMD;
    }

    return 0;
}

// process <fwd> messages
int process_fwd(char* token, char* saveptr1, udpfwd_t* udpFwdList, fd_set* base_rfds){
    char fwdAddr[16] = "", fwdPort[6] = "", udpListen[6] = "";
    int i = 0, j = 0, udpNo = 0, fwdNo = 0;

    // check if MAX_UDPLISTEN limit is reached 
    for (udpNo = 0; udpNo < MAX_UDPLISTEN; udpNo++)
        if (udpFwdList[udpNo].fd == -1) break;
    
    if (udpNo == MAX_UDPLISTEN) 
    {
        if (DEBUG) fprintf(stderr, "UDP rule limit reached! (max: %d)\n", MAX_UDPLISTEN);
        return UDPLIMIT;
    }

    // get port number to listen at
    if ((token = strtok_r(NULL, " ", &saveptr1)) == NULL)
    {
        if (DEBUG) fprintf(stderr, "fwd command parameters missing!\n");
        return INVLDCMD;
    }
     
    //check port number is ok
    if (validatePort(token, 1) < 0) return INVLDPRT;

    strncpy(udpListen, token, strlen(token));
    if (DEBUG) fprintf(stderr, "Listen port: -%s-\n", udpListen);

    //check for duplicate rule
    for (j = 0; j < MAX_UDPLISTEN; j++)
    {
        if (udpFwdList[j].fd == -1 || strtol(udpListen, NULL, 10) != strtol(udpFwdList[j].port, NULL, 10)) continue;
        if (DEBUG) fprintf(stderr, "There's already a rule defined for this port! Close it first.\n");
        return DPLCTRUL;
    }

    // process each <ip:port> to forward to
    while(1)
    {
        int err=0, ret=0;
        
        token = strtok_r(NULL, " ", &saveptr1);
        if (token == NULL) break;

        if (fwdNo == MAX_UDPFWD) 
        {
            if (DEBUG) fprintf(stderr, "UDP rule lists too many forward addresses (max:%d)!\n", MAX_UDPFWD);
            return FWDLIMIT;
        }

        // ip:port
        if (DEBUG) fprintf(stderr, "[%d] ip:port = %s\n", ++i, token);
        if((ret = validateIpPort(token, fwdAddr, fwdPort)) < 0) return ret;

        // <ip:port> has no errors, make address and add to forwarding list		
        if (DEBUG) fprintf(stderr, "[%d] %s:%s\n", fwdNo+1, fwdAddr, fwdPort);
        udpFwdList[udpNo].fwdList[fwdNo] = make_address(fwdAddr, fwdPort, &err);
        if(err < 0) 
        {
            if (DEBUG) fprintf(stderr, "Cannot connect to %s:%s\n", fwdAddr, fwdPort);
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

    if (DEBUG) fprintf(stderr, "Forward addresses in the rule: %d\n", udpFwdList[udpNo].fwdCount);

    return 0;
}

// process <close> messages
int process_close(char* token, char* saveptr1, udpfwd_t* udpFwdList, fd_set* base_rfds){
    int j = 0;
    char udpListen[6] = "";

    // get port number to close
    if ((token = strtok_r(NULL, " ", &saveptr1)) == NULL)
    {
        if (DEBUG) fprintf(stderr, "Port number missing!\n");
        return INVLDCMD;
    }
    
    //check port number is a number
    if (isNumeric(token) < 0)
    {
        if (DEBUG) fprintf(stderr, "Error in udp listen port number!\n");
        return INVLDPRT;
    }

    strncpy(udpListen, token, strlen(token));
    if (DEBUG) fprintf(stderr, "Listen port to close: -%s-\n", udpListen);
    
    for (j = 0; j < MAX_UDPLISTEN; j++)
    {
        // skip until finding the position of the port in the array
        if (udpFwdList[j].fd == -1 || strtol(udpListen, NULL, 10) != strtol(udpFwdList[j].port, NULL, 10)) continue;
        
        // remove udp from base_rdfs, close socket and set fd to -1
        FD_CLR(udpFwdList[j].fd, base_rfds);
        if (TEMP_FAILURE_RETRY(close(udpFwdList[j].fd)) < 0) ERR("close");
        udpFwdList[j].fd = -1;
        if (DEBUG) fprintf(stderr, "Socket closed\n");
        return 0;
    }

    if (DEBUG) fprintf(stderr, "There's no open udp socket with port number %s\n", udpListen);
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
        if (DEBUG) fprintf(stderr, "Array pos: %d - file desc: %d\n", j, udpFwdList[j].fd);
        
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
    }

    return 0;
}

// send error message to client
void sendErrorMsg(int cfd, int errCode){
    char buf[11] = "";

    switch (errCode)
    {
        case INVLDCMD:
            if(bulk_write(cfd, invldcmd, sizeof(invldcmd)) < 0 && errno!=EPIPE) ERR("write:");
            break;
        case UDPLIMIT:
            if(bulk_write(cfd, udplimit, sizeof(udplimit)) < 0 && errno!=EPIPE) ERR("write:");
            sprintf(buf, "(Max:%d)\n", MAX_UDPLISTEN);
            if(bulk_write(cfd, buf, sizeof(buf)) < 0 && errno!=EPIPE) ERR("write:");
            break;
        case INVLDPRT:
            if(bulk_write(cfd, invldprt, sizeof(invldprt)) < 0 && errno!=EPIPE) ERR("write:");
            break;
        case DPLCTRUL:
            if(bulk_write(cfd, dplctrul, sizeof(dplctrul)) < 0 && errno!=EPIPE) ERR("write:");
            break;
        case FWDLIMIT:
            if(bulk_write(cfd, fwdlimit, sizeof(fwdlimit)) < 0 && errno!=EPIPE) ERR("write:");
            sprintf(buf, "(Max:%d)\n", MAX_UDPFWD);
            if(bulk_write(cfd, buf, sizeof(buf)) < 0 && errno!=EPIPE) ERR("write:");
            break;
        case MAKEADDR:
            if(bulk_write(cfd, makeaddr, sizeof(makeaddr)) < 0 && errno!=EPIPE) ERR("write:");
            break;
        case NOSOCKET:
            if(bulk_write(cfd, nosocket, sizeof(nosocket)) < 0 && errno!=EPIPE) ERR("write:");
            break;
        case INVLDIP:
            if(bulk_write(cfd, invldip, sizeof(invldip)) < 0 && errno!=EPIPE) ERR("write:");
            break;
    }
}

// process messages from clients
int process_msg(char* msg, udpfwd_t* udpFwdList, fd_set* base_rfds, int cfd){
    int ret;
    char *token, *saveptr1;
    
    token = strtok_r(msg, " ", &saveptr1);

    if (strcmp(token, "fwd") == 0)
    {
        // stage 4
        if ((ret = process_fwd(token, saveptr1, udpFwdList, base_rfds)) < 0) return ret;
        
        if(bulk_write(cfd, sockopen, sizeof(sockopen)) < 0 && errno!=EPIPE) ERR("write:");
        return 0;
        
    }
    else if (strcmp(token, "close") == 0)
    {
        // stage 5
        if ((ret = process_close(token, saveptr1, udpFwdList, base_rfds)) < 0) return ret;
        
        if(bulk_write(cfd, sckclose, sizeof(sckclose)) < 0 && errno!=EPIPE) ERR("write:");
        return 0;
    }
    else if(strcmp(token, "show") == 0)
    {
        // stage 6
        return(sendFwdInfo(cfd, udpFwdList));
    }
    else return INVLDCMD;	
}

// server work
void doServer(int sfd){
    int cfd = 0, ret = 0, tcpCons=0, udpCons = 0, tcpFd[MAX_TCP];
    char buf[MAXBUF] = "";
    fd_set base_rfds, rfds;
    sigset_t mask, oldmask;
    udpfwd_t udpFwdList[MAX_UDPLISTEN];
    
    // set all file descriptors to -1
    for (int i = 0; i < MAX_TCP; i++) tcpFd[i] = -1;
    for (int i = 0; i < MAX_UDPLISTEN; i++) udpFwdList[i].fd = -1;
    
    // set base_rfds once and use in the loop to reset rfds
    FD_ZERO(&base_rfds);
    FD_SET(sfd, &base_rfds);
    
    // set signal mask for pselect
    sigemptyset (&mask);
    sigaddset (&mask, SIGINT);
    sigprocmask (SIG_BLOCK, &mask, &oldmask);

    int fdU = make_socket(PF_INET, SOCK_DGRAM); // udp send socket
    
    while(do_work) // until SIGINT
    {
        rfds = base_rfds;
        
        if (pselect(getMaxFd(udpFwdList, tcpFd, sfd) + 1, &rfds, NULL, NULL, NULL, &oldmask) > 0) // FD_SETSIZE bad??
        {
            //  new client connection
            if (FD_ISSET(sfd, &rfds) && (cfd = add_new_client(sfd)) > 0)
            {
                // if less than 3 clients add new client
                if (tcpCons < 3)
                {
                    int added = 0;
                    for (int i = 0; i < MAX_TCP; i++) 
                    {
                        if (tcpFd[i] == -1)
                        {
                            tcpFd[i] = cfd;
                            FD_SET(cfd, &base_rfds);
                            added = 1;
                            tcpCons++;
                            fprintf(stderr, "Client connected. [%d]\n", tcpCons);
                            break;
                        }
                    }
                    if (!added) ERR("Connection add error");
                    
                    if(bulk_write(cfd, hellomsg, sizeof(hellomsg)) < 0 && errno!=EPIPE) ERR("write:");
                }
                else // max. clients reached. send info msg.
                {
                    memset(buf, 0, sizeof(buf));
                    
                    if (bulk_write(cfd, tcpfull, sizeof(tcpfull)) < 0 && errno!=EPIPE) ERR("write:");
                    sprintf(buf, "(Max:%d)\n", MAX_TCP);
                    if(bulk_write(cfd, buf, sizeof(buf)) < 0 && errno!=EPIPE) ERR("write:");
                    
                    if (DEBUG) fprintf(stderr, "Max. clients already connected (%d). Client connection request refused.\n", MAX_TCP);
                    if (TEMP_FAILURE_RETRY(close(cfd)) < 0) ERR("close");
                }            
            }        
            
            // check incoming tcp message
            for (int i = 0; i < MAX_TCP; i++)
            {
                memset(buf, 0, sizeof(buf));

                if (FD_ISSET(tcpFd[i], &rfds))
                {
                    // if recv() call returns zero connection is closed on the other side
                    if(recv(tcpFd[i], buf, MAXBUF, MSG_PEEK) == 0) 
                    {
                        tcpCons--;
                        fprintf(stderr, "Client disconnected. Closing socket. [%d left]\n", tcpCons);
                        FD_CLR(tcpFd[i], &base_rfds);
                        if (TEMP_FAILURE_RETRY(close(tcpFd[i])) < 0) ERR("close");
                        tcpFd[i] = -1;
                    }
                    else // read and process message
                    {
                        if ((ret = recv(tcpFd[i], buf, MAXBUF, 0)) < 0) ERR("recv"); 
                        buf[ret-2] = '\0'; // remove endline char
                        
                        if (DEBUG) fprintf(stderr, "RECEIVED MESSAGE: --%s-- with size %d\n", buf, ret);
                        
                        //process incoming message
                        if((ret = process_msg(buf, udpFwdList, &base_rfds, tcpFd[i])) < 0) 
                        {
                            // there's an error in message
                            sendErrorMsg(tcpFd[i], ret);
                        }
                    }
                }
            }

            // check incoming udp message
            for (int i = 0; i < MAX_UDPLISTEN; i++)
            {
                memset(buf, 0, sizeof(buf));

                if (FD_ISSET(udpFwdList[i].fd, &rfds))
                {
                    //receive udp message
                    if((ret = recv(udpFwdList[i].fd, buf, MAXBUF, 0)) < 0) ERR("udp read");
                    buf[ret] = '\0';
                    if (DEBUG) fprintf(stderr, "%s", buf);

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
        udpCons--;
        fprintf(stderr, "Closing udp listen socket. [%d left]\n", udpCons);
    }

    // close open tcp sockets
    for (int i = 0; i < MAX_TCP; i++) 
    {
        if (tcpFd[i] < 0) continue;
        if (TEMP_FAILURE_RETRY(close(tcpFd[i])) < 0) ERR("close");
        tcpCons--;
        fprintf(stderr, "Closing tcp send socket. [%d left]\n", tcpCons);
    }

    sigprocmask (SIG_UNBLOCK, &mask, NULL);
}

int main(int argc, char** argv){
    int sfd;
    int new_flags;
    
    if(argc!=2) 
    {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    if(sethandler(SIG_IGN,SIGPIPE)) ERR("Setting SIGPIPE:");
    if(sethandler(sigint_handler,SIGINT)) ERR("Setting SIGINT:");
    
    // make and bind tcp listen socket
    sfd=bind_inet_socket(strtol(argv[1], NULL, 10), SOCK_STREAM);
    
    // set tcp listen socket to nonblocking
    new_flags = fcntl(sfd, F_GETFL) | O_NONBLOCK;
    fcntl(sfd, F_SETFL, new_flags);
    
    // accept connections and start working!
    doServer(sfd);
    
    // close tcp listen socket
    if (TEMP_FAILURE_RETRY(close(sfd)) < 0) ERR("close");
    fprintf(stderr, "Closing tcp listen socket.\n");
    fprintf(stderr, "Server has terminated.\n");
    return EXIT_SUCCESS;
}