#define _GNU_SOURCE
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#define ERR(source) (fprintf(stderr,"%s:%d\n",__FILE__,__LINE__),\
                     perror(source),kill(0,SIGKILL),\
             exit(EXIT_FAILURE))

//MAX_BUFF must be in one byte range
#define MAX_BUFF 200

volatile sig_atomic_t last_signal = 0;

void usage(char * name)
{
    fprintf(stderr, "USAGE: %s n\n", name);
    fprintf(stderr, "0<n<=10 - number of children\n");
    exit(EXIT_FAILURE);
}

int sethandler( void (*f)(int), int sigNo) 
{
    struct sigaction act;
    memset(&act, 0, sizeof(struct sigaction));
    act.sa_handler = f;
    if (-1 == sigaction(sigNo, &act, NULL))
        return -1;
    return 0;
}

void sig_handler(int sig) 
{
    last_signal = sig;
}

void sig_killme(int sig) 
{
    if (rand() % 5 == 0)
        exit(EXIT_SUCCESS);
}


void sigchld_handler(int sig) 
{
    pid_t pid;
    for(;;)
    {
        pid = waitpid(0, NULL, WNOHANG);
        if (0 == pid) return;
        if (0 >= pid) 
        {
            if (ECHILD == errno) return;
            ERR("waitpid:");
        }
    }
}

void child_work(int fd, int R) 
{
    char c, buf[MAX_BUFF+1];
    unsigned char s;
    srand(getpid());
    
    // set SIGINT handler (20% chance of kill) for child
    if (sethandler(sig_killme, SIGINT)) ERR("Setting SIGINT handler in child");
    
    for(;;)
    {
        if (TEMP_FAILURE_RETRY(read(fd, &c, 1)) < 1) ERR("read");
        s = 1 + rand() % MAX_BUFF;
        buf[0] = s;
        memset(buf+1, c, s);
        if (TEMP_FAILURE_RETRY(write(R,buf,s+1)) < 0) ERR("write to R");
    }
}

void parent_work(int n, int *fds, int R) 
{
    unsigned char c;
    char buf[MAX_BUFF];
    int status, i;
    srand(getpid());
    
    if (sethandler(sig_handler, SIGINT)) ERR("Setting SIGINT handler in parent");
    
    for(;;)
    {
        if (SIGINT == last_signal)
        {
            i = rand() % n; // select random child
            
            // if child is inactive (fd==0) try to find an active child (maybe none left)
            while(0 == fds[i%n] && i < 2*n) i++;            
            i %= n; 

            if (fds[i])
            {
                c = 'a' + rand() % ('z'-'a');
                
                status = TEMP_FAILURE_RETRY(write(fds[i], &c, 1));
                if (status != 1) // if couldn't write close the fd for parent
                {
                    if (TEMP_FAILURE_RETRY(close(fds[i]))) ERR("close");
                    fds[i]=0;
                }
            }

            last_signal=0;
        }

        status = read(R, &c, 1); // read first byte (size of message) into c
        if (0 == status) break; // no child left
        if (status < 0 && errno == EINTR) continue; // IMPORTANT! cannot use TEMP_FAILURE_RETRY becasue SIGINT has a function and must be handled
        if (status < 0) ERR("read header from R");

        //read remaining c bytes to buf 
        if (TEMP_FAILURE_RETRY(read(R, buf, c)) < c) ERR("read data from R");
        
        buf[(int)c]=0; // set end of array to zero
        printf("\n%s\n", buf); 
    }    
}

void create_children_and_pipes(int n, int *fds, int R) 
{
    int tmpfd[2];
    int max = n;
    while (n) 
    {
        if (pipe(tmpfd)) ERR("pipe");

        switch (fork()) 
        {
            case 0:
                // close fds of previous childs
                while(n < max) 
                    if (fds[n] && TEMP_FAILURE_RETRY(close(fds[n++]))) ERR("close");
                free(fds);
                
                //close write end for child
                if (TEMP_FAILURE_RETRY(close(tmpfd[1]))) ERR("close");
                // send read fd and write R to child
                child_work(tmpfd[0],R);

                // close read fd for child
                if (TEMP_FAILURE_RETRY(close(tmpfd[0]))) ERR("close");
                // close write R for child
                if (TEMP_FAILURE_RETRY(close(R))) ERR("close");
                exit(EXIT_SUCCESS);

            case -1: ERR("Fork:");
        }
        //close read end for parent
        if (TEMP_FAILURE_RETRY(close(tmpfd[0]))) ERR("close");
        // put child's read fd into fds so parent can use it to write to
        fds[--n]=tmpfd[1];
    }
}

int main(int argc, char** argv) 
{
    int n, *fds, R[2];
    if (2 != argc) usage(argv[0]);
    
    n = atoi(argv[1]);
    if (n<=0 || n>10) usage(argv[0]);
    
    if (sethandler(SIG_IGN, SIGINT)) ERR("Setting SIGINT handler");
    if (sethandler(SIG_IGN, SIGPIPE)) ERR("Setting SIGINT handler");
    if (sethandler(sigchld_handler, SIGCHLD)) ERR("Setting parent SIGCHLD:");
    
    if (pipe(R)) ERR("pipe");
    if (NULL==(fds=(int*)malloc(sizeof(int)*n))) ERR("malloc");
    
    create_children_and_pipes(n,fds,R[1]); // fds for parent to write, R[0] to listen
    
    if (TEMP_FAILURE_RETRY(close(R[1]))) ERR("close");
    
    parent_work(n,fds,R[0]);
    
    while (n--) if (fds[n] && TEMP_FAILURE_RETRY(close(fds[n]))) ERR("close");
    if (TEMP_FAILURE_RETRY(close(R[0]))) ERR("close"); // close read R[0] for parent
    
    free(fds);
    return EXIT_SUCCESS;
}
