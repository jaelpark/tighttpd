#include "main.h"
#include "socket.h"
#include "protocol.h"

#include <csignal>
#include <stdlib.h>

#include <time.h> //logging gimmick
#include <stdarg.h>

#include <sys/epoll.h>

#include <queue>

#include <tbb/tbb.h>
#include <tbb/flow_graph.h>

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

	event1.data.ptr = &server;
	event1.events = EPOLLIN;
	epoll_ctl(efd,EPOLL_CTL_ADD,sfd,&event1);

	std::queue<Protocol::ClientProtocol *> taskq; //task queue for intensive work
	for(;;){
		//TODO: if work queue is empty, block indefinitely (-1). Otherwise return immediately (0).
		for(int n = epoll_wait(efd,events,MAX_EVENTS,-1), i = 0; i < n; ++i){
			if(events[i].data.ptr == &server){
				for(;;){
					Socket::ClientSocket client(server.Accept());
					if(client.fd == -1)
						break;

					Protocol::ClientProtocolHTTP *ptp =
						new Protocol::ClientProtocolHTTP(client);

					if(ptp->Poll(PROTOCOL_ACCEPT))
						taskq.push(ptp);

					event1.data.ptr = ptp;
					event1.events =
						(ptp->sflags & PROTOCOL_RECV?EPOLLIN:0)|
						(ptp->sflags & PROTOCOL_SEND?EPOLLOUT:0);
					epoll_ctl(efd,EPOLL_CTL_ADD,client.fd,&event1);
				}
			}else{
				Protocol::ClientProtocolHTTP *ptp = (Protocol::ClientProtocolHTTP *)events[i].data.ptr;
				uint sflags = ptp->sflags;

				if(ptp->sflags & PROTOCOL_RECV && events[i].events & EPOLLIN && ptp->psp->Read()){
					if(ptp->Poll(PROTOCOL_RECV))
						taskq.push(ptp);
				}

				if(ptp->sflags & PROTOCOL_SEND && events[i].events & EPOLLOUT && ptp->psp->Write()){
					if(ptp->Poll(PROTOCOL_SEND))
						taskq.push(ptp);
				}

				//dynamically enable both EPOLLIN and EPOLLOUT depending on the state
				Socket::ClientSocket client = ptp->socket;
				if(sflags != ptp->sflags){
					event1.data.ptr = ptp;
					event1.events =
						(ptp->sflags & PROTOCOL_RECV?EPOLLIN:0)|
						(ptp->sflags & PROTOCOL_SEND?EPOLLOUT:0);
					epoll_ctl(efd,EPOLL_CTL_MOD,client.fd,&event1);
				}
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
