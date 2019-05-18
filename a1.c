#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h> 
#include <sys/wait.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h> // non-blocking sockets
#include <stdbool.h>

#define BUFSIZE 1024
#define PSIGUSR1 10
#define PSIGUSR2 20
#define NEEDFORK 30
#define RESPFORK 40
#define FINISHEDEATING 50

#define CHECK 100
#define RELEASE 200
#define CHECKDONE 300
#define RELEASEDONE 400

//char* philosopher;
//char*  left;
//char* right;
pthread_t listen1;
pthread_t connect1;
pthread_t start_thread;
pthread_t fork_server_id;
pthread_t masterthread;

int start = 0;
int stop = 0;

pthread_mutex_t mutex;
int fready;
pthread_cond_t cond;

pthread_mutex_t fmutex;
int myfork = 1;

char phil_state = 'W';  // waiting, Thinking, eating, starving, absent.

pid_t phil_pid;

int Thinking_time;
int Eating_time;

// listento variables
int listen_fd;
int right_fd;
int portno = 7789;
int rightlen;

struct in_addr ipv4addr;
struct sockaddr_in listenaddr;
struct sockaddr_in rightaddr;
struct hostent* righthostp;  // left host info
char* righthostaddrp; // dotted decimal left host addr string
int optval, n;

// client side
int left_fd;
struct sockaddr_in serveraddr;
struct hostent *server;
char* hostname;

int philid;

void error (char* msg) {
	perror(msg);
	exit(0);
}

typedef struct MSG
{
    int mtype;
    int fstate;
}MSG;

void initmsg(MSG *msg, int type, int state)
{
    msg->mtype = type;
    msg->fstate = state;
}

void my_signal_handler(int signum)
{
    MSG msg;
    if (signum == SIGUSR1)
    {
        printf("Received SIGUSR1 Signal!\n");
        printf("**********************************************************\n");
        
        phil_state = 'T';
        
        bzero((char *) &msg,sizeof(MSG));
        initmsg(&msg,PSIGUSR1,0);
        if(write(left_fd,&msg,sizeof(MSG)) == -1) {
            perror("write");
        } else {
            printf("message sent to left Neighbore: SIGUSR1\n");
        }
        start = 1;
        
        
    } else if (signum == SIGUSR2) {
        printf("Received SIGUSR2!\n");
        printf("**********************************************************\n");
        printf("philosopher closing sockets and terminating\n");
        
        bzero((char *) &msg,sizeof(MSG));
        initmsg(&msg,PSIGUSR2,0);
        if(write(left_fd,&msg,sizeof(MSG)) == -1) {
            perror("write");
        } else {
            printf("message sent to left Neighbore: SIGUSR2\n");
        }
        stop = 1;
        
        sleep(2);
        close(left_fd);
        close(right_fd);
        exit(0);

    }
}

void print() {
    printf("Error: Please follow the syntax to run the program\n");
    printf("./a.out  <phil_ip>  <left phil_ip> <right phil_ip>\n");
    printf("where: \n");
    printf("<phil_ip>      : ip-address of the philosopher.\n");
    printf("<left phil_ip> : ip-address of the philosopher to your left.\n");
    printf("<right phil_ip>: ip-address of the philosopher to your right.\n");
    return;
}

void* listento(void *argv) {	// server
	printf("listern thread: starting...\n");
	// create parent socket
	listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if(listen_fd < 0)
		error("ERROR opening socket");

	optval = 1;
	setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval, sizeof(int));
	
	// build the server internet address
	bzero((char *) &listenaddr, sizeof(listenaddr));
	listenaddr.sin_family = AF_INET;
	listenaddr.sin_addr.s_addr = htonl(INADDR_ANY);
	listenaddr.sin_port = htons((unsigned short) portno);
	
	// bind: asociate parent socket with port
	if(bind(listen_fd,(struct sockaddr *) &listenaddr, sizeof(listenaddr)) < 0)
		error("ERROR on binding");

	// listen: make socket ready to accept connection requests
	if(listen(listen_fd,5) < 0) {
		error("ERROR on listen");
	}

	rightlen = sizeof(rightaddr);
	int x = 1;
    for(;;) {
        sleep(1);
	    printf("listern thread: waiting to accept...\n");
   	    right_fd = accept4(listen_fd,(struct sockaddr *) & rightaddr, &rightlen, SOCK_NONBLOCK);
    	if(right_fd < 0) {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
    	} else {
            printf("Connected...\n");
            break;
    	}
    }
    	printf("listener: connected to neighbor. sock is [%d] \n", right_fd);
}

void *phil_start(void *arg) {
    
    MSG msg;
    while(start == 0)
    {
        bzero(&msg,sizeof(MSG));
        n = read(right_fd, &msg,sizeof(MSG));
        if (n < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                continue;
            }
        } else if (n > 0) {
            if(msg.mtype == PSIGUSR1){
                bzero((char *) &msg,sizeof(MSG));
                initmsg(&msg,PSIGUSR1,0);
                if(write(left_fd,&msg,sizeof(MSG)) == -1) {
                    perror("write");
                } else {
                    printf("message sent to left Neighbore: SIGUSR1\n");
                }

                if(start == 0)
                    start = 1;
                phil_state = 'T';
                sleep(1);
            }
        }    
    }
}

void* connectto(void* argv) {
	
	int left_connected = 0;
	printf("connectthread: starting ...\n");
	
	// create socket
	left_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
	if(left_fd < 0)
		error("ERROR opening socket");

	// gethostbyname: get the server's DNS entry
        server = gethostbyname(hostname);
	if (server == NULL) {
		fprintf(stderr,"ERROR, no such host as %s\n", hostname);
                exit(0);
    }

	bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
	bcopy((char *) server->h_addr, (char *) &serveraddr.sin_addr.s_addr,server->h_length);
	serveraddr.sin_port = htons(portno);

	int tried = 0;
	while(left_connected == 0 && tried <= 200) {
		if(connect(left_fd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
			tried++;
            sleep(1);
		} else {
			left_connected = 1;
		
        }
    }
	printf("Connect thread: connected sock fd is [%d]\n", left_fd);
}

int retval;
int ret1;
int ret2;

void *fork_server(void *argc);

int main(int argc, char **argv) {

    MSG msg;
  
    if(argc < 3) {
        print();
        exit(0);
    }
    phil_pid = getpid();
    philid = atoi(argv[1]);
	hostname = argv[2];

	retval = signal(SIGUSR1, my_signal_handler);
    if(retval == SIG_ERR) {
        printf("SIGUSR1 signal register error\n");
        exit(1);
    }
    retval = signal(SIGUSR2, my_signal_handler);
    if(retval == SIG_ERR) {
        printf("SIGUSR2 signal register error\n");
        exit(1);
    }

    ret1 = pthread_create(&listen1, NULL, listento, NULL );
    sleep(3);
    ret2 = pthread_create(&connect1, NULL, connectto, NULL);
    ret1 = pthread_create(&start_thread, NULL, phil_start, NULL );
    pthread_join(listen1,NULL);   
    pthread_join(connect1,NULL);   
    printf("Thread listen1 returns: %d\n",ret1);
    printf("Thread connect1 returns: %d\n",ret2);
    while (phil_state != 'T')
    {
        printf("Philosopher %s waiting pid [%d]\n", argv[1], getpid());
        sleep(1);
    }

    ret1 = pthread_create(&fork_server_id, NULL, fork_server, NULL );
    for(;;)
    {
        while(phil_state != 'E'){
            srand(time(0));
            Thinking_time = (rand() % (15 - 5 + 1)) + 5; 
            printf("Thinking time = %d\n",Thinking_time);
            while(Thinking_time > 0){
                printf("philosopher %s thinking\n",argv[1]);
                sleep(1);
                Thinking_time --;
            }
            
            pthread_mutex_lock(&mutex);
            fready = CHECK;
            while(fready != CHECKDONE)
                pthread_cond_wait(&cond, &mutex);
            pthread_mutex_unlock(&mutex);

            if(phil_state = 'E')
            {
                Eating_time = (rand() % (10 - 5 + 1)) + 5;
                printf("Eating time = %d\n", Eating_time);
                while(Eating_time > 0){
                    printf("philosopher %s eating\n",argv[1]);
                    sleep(1);
                    Eating_time --;
                }
                pthread_mutex_lock(&mutex);
                fready = RELEASE;
                while(fready != RELEASEDONE)
                    pthread_cond_wait(&cond, &mutex);
                pthread_mutex_unlock(&mutex);

                phil_state = 'T';
            }
        }
    }

    pthread_join(fork_server_id,NULL);   

    return 0;
}

void *fork_server(void *argc){

    MSG lmsg;
    MSG rmsg;
    int mystate;

    while(1){
        pthread_mutex_lock(&mutex);
        if(fready == CHECK){
            int lfork = 0;
            int rfork = 0;
            if((pthread_mutex_trylock(&fmutex)) == 0)
            {
                if(myfork == 1)
                {
                    myfork = 0;
                    lfork = 1;
                }
                else
                {
                    //myfork = 1;
                    lfork = 0;
                }
                pthread_mutex_unlock(&fmutex);
            } 
            if(lfork == 1)
            {
                bzero((char *) &lmsg,sizeof(MSG));
                initmsg(&lmsg,NEEDFORK,0);
                if(write(left_fd,&lmsg,sizeof(MSG)) == -1) {
                    perror("write");
                } else {
                    ;
                }

                bzero((char *) &lmsg,sizeof(MSG));
                n = read(left_fd, &lmsg,sizeof(MSG));
                if (n < 0)
                {
                    if(errno == EAGAIN || errno == EWOULDBLOCK)
                    {
                        ;
                    }
                }
                
                if(lmsg.mtype == RESPFORK)
                {
                    if(lmsg.fstate == 1)
                    {
                        rfork = 1;
                    } else {
                        if(pthread_mutex_trylock(&fmutex) == 0)
                        {
                            myfork = 1;
                            lfork = 0;
                            rfork = 0;
                            pthread_mutex_unlock(&fmutex);
                        }
                        else
                        {
                            ;
                        }
                    }
                } 

            }
	        else
            {
                printf("Local fork not available\n");
            }

            if(lfork == 1 && rfork == 1){
                printf("BOTH FORKS AVAILABLE. WILL EAT NOW\n");
                phil_state = 'E';
            }

            fready = CHECKDONE;
            pthread_cond_broadcast(&cond);
        } else if(fready == RELEASE){
            bzero((char *) &lmsg,sizeof(MSG));
            initmsg(&lmsg,FINISHEDEATING,1);
            if(write(left_fd,&lmsg,sizeof(MSG)) == -1) {
                perror("write");
            } else {
                ;
            }
            //if(pthread_mutex_trylock(&fmutex) == 0)
            if(pthread_mutex_lock(&fmutex) == 0)
            {
                myfork = 1;

            }
                pthread_mutex_unlock(&fmutex);
        
            fready = RELEASEDONE;
            pthread_cond_broadcast(&cond);
        }
        pthread_mutex_unlock(&mutex);

        bzero((char *) &rmsg,sizeof(MSG));
        n = read(right_fd, &rmsg,sizeof(MSG));
        if (n < 0)
        {
            if(errno == EAGAIN || errno == EWOULDBLOCK)
            {
                 ;
            }
            else if(n > 0){
                if(rmsg.mtype == NEEDFORK)
                {
                    int mystate = 0;
                    if((pthread_mutex_trylock(&fmutex)) == 0)
                    {
                        if(myfork == 1)
                        {
                            mystate = 1;
                            myfork = 0;
                        } else {
                            mystate = 0;
                            //myfork = 1;
                        }
                        pthread_mutex_unlock(&fmutex);
                    }

                    bzero((char *) &rmsg,sizeof(MSG));
                    initmsg(&rmsg,RESPFORK,mystate);
                    if(write(right_fd,&rmsg,sizeof(MSG)) == -1) {
                            perror("write");
                    } else {
                            printf("message sent to left Neighbore: SIGUSR1\n");
                    }
                } else if(rmsg.mtype == FINISHEDEATING){
                    if((pthread_mutex_trylock(&fmutex)) == 0)
                    {
                        myfork = 1;
                        pthread_mutex_unlock(&fmutex);
                    }
                }
                if(rmsg.mtype == PSIGUSR2){
                    bzero((char *) &lmsg,sizeof(MSG));
                    initmsg(&lmsg,PSIGUSR2,0);
                    if(write(left_fd,&lmsg,sizeof(MSG)) == -1) {
                        perror("write");
                    } else {
                        printf("message sent to left Neighbore: SIGUSR2\n");
                    }

                    if(stop == 0)
                        stop = 1;
                    phil_state = 'S';
                    close(left_fd);
                    close(right_fd);
                    exit(0);
                }
            }
        }
    }
}
