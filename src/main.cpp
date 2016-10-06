#include "main.h"
#include "socket.h"

#include <csignal>
#include <stdlib.h>

#include <time.h> //logging gimmick
#include <stdarg.h>

#include <sys/epoll.h>

int main(int argc, const char **pargv){

	signal(SIGINT,[](int s)->void{
		DebugPrintf("Received SIGINT\n");
		exit(0);
	});

	Socket::ServerSocket server(0);
	int sfd = server.Listen();

	//Create the epoll socket monitoring instance
	//-> man epoll
#define MAX_EVENTS 1024
	struct epoll_event event1, events[MAX_EVENTS];
	int efd = epoll_create1(0);
	if(efd == -1){
		DebugPrintf("Error: epoll_create\n");
		return -1;
	}

	event1.events = EPOLLIN|EPOLLET;
	event1.data.fd = sfd;
	epoll_ctl(efd,EPOLL_CTL_ADD,sfd,&event1);

	//main server loop
	for(;;){
		int n = epoll_wait(efd,events,MAX_EVENTS,-1);
		for(int i = 0; i < n; ++i){

			if(events[i].data.fd == sfd){
				//Event refers to server socket, meaning we have new incoming connections
				for(;;){
					int cfd = server.Accept();
					if(cfd == -1)
						break;

					event1.data.fd = cfd;
					event1.events = EPOLLIN|EPOLLET;
					epoll_ctl(efd,EPOLL_CTL_ADD,cfd,&event1);

				}
			}else{
				//Incoming data
				//
			}
		}
	}

	return 0;
}

void DebugPrintf(const char *pfmt, ...){
	time_t rt;
	time(&rt);
	const struct tm *pti = localtime(&rt);

	char tbuf[256];
	strftime(tbuf,sizeof(tbuf),"[tighttpd %F %T]",pti);
	printf("%s ",tbuf);

	va_list args;
	va_start(args,pfmt);
	vprintf(pfmt,args);
	va_end(args);
}
