#include "main.h"
#include "socket.h"
#include "protocol.h"

#include <csignal>
#include <stdlib.h>

#include <time.h> //logging gimmick
#include <stdarg.h>

#include <sys/epoll.h>

#include <tbb/flow_graph.h>

int main(int argc, const char **pargv){
	signal(SIGINT,[](int s)->void{
		DebugPrintf("Received SIGINT\n");
		exit(0);
	});

	const char *pport = argc > 1?pargv[1]:"8080";

	Socket::ServerSocket server(0);
	int sfd = server.Listen(pport);

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

					switch(ptp->Poll(PROTOCOL_ACCEPT)){
					case Protocol::ClientProtocol::POLL_RUN:
						taskq.push(ptp);
						break;
					case Protocol::ClientProtocol::POLL_CLOSE:
						epoll_ctl(efd,EPOLL_CTL_DEL,client.fd,0);
						delete ptp;
						continue;
					}

					event1.data.ptr = ptp;
					event1.events =
						(ptp->sflags & PROTOCOL_RECV?EPOLLIN:0)|
						(ptp->sflags & PROTOCOL_SEND?EPOLLOUT:0);
					epoll_ctl(efd,EPOLL_CTL_ADD,client.fd,&event1);
				}
			}else{
				Protocol::ClientProtocolHTTP *ptp = (Protocol::ClientProtocolHTTP *)events[i].data.ptr;
				Socket::ClientSocket client = ptp->socket;

				uint sflags = ptp->sflags;

				if(ptp->sflags & PROTOCOL_RECV && events[i].events & EPOLLIN && ptp->psp->Read()){
					switch(ptp->Poll(PROTOCOL_RECV)){
					case Protocol::ClientProtocol::POLL_RUN:
						taskq.push(ptp);
						break;
					case Protocol::ClientProtocol::POLL_CLOSE:
						epoll_ctl(efd,EPOLL_CTL_DEL,client.fd,0);
						delete ptp;
						continue;
					}
				}

				if(ptp->sflags & PROTOCOL_SEND && events[i].events & EPOLLOUT && ptp->psp->Write()){
					switch(ptp->Poll(PROTOCOL_SEND)){
					case Protocol::ClientProtocol::POLL_RUN:
						taskq.push(ptp);
						break;
					case Protocol::ClientProtocol::POLL_CLOSE:
						epoll_ctl(efd,EPOLL_CTL_DEL,client.fd,0);
						delete ptp;
						continue;
					}
				}

				//dynamically enable both EPOLLIN and EPOLLOUT depending on the state
				if(sflags != ptp->sflags){
					event1.data.ptr = ptp;
					event1.events =
						(ptp->sflags & PROTOCOL_RECV?EPOLLIN:0)|
						(ptp->sflags & PROTOCOL_SEND?EPOLLOUT:0);
					epoll_ctl(efd,EPOLL_CTL_MOD,client.fd,&event1);
				}
			}
		}

		for(; !taskq.empty();){
			Protocol::ClientProtocol *ptp = taskq.front();
			uint sflags = ptp->sflags;

			ptp->Run();

			if(sflags != ptp->sflags){
				event1.data.ptr = ptp;
				event1.events =
					(ptp->sflags & PROTOCOL_RECV?EPOLLIN:0)|
					(ptp->sflags & PROTOCOL_SEND?EPOLLOUT:0);
				epoll_ctl(efd,EPOLL_CTL_MOD,ptp->socket.fd,&event1);
			}

			taskq.pop();
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
