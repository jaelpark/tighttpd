#include "main.h"

#include <string.h>
#include <stdio.h>

#include <csignal>

#include <time.h> //logging gimmick
#include <stdarg.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>

static void DebugPrintf(const char *pfmt, ...){
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

int main(int argc, const char **pargv){

	struct sigaction siginth;
	siginth.sa_handler = [](int s)->void{DebugPrintf("Received SIGINT\n");};
	siginth.sa_flags = 0;
	sigemptyset(&siginth.sa_mask);
	sigaction(SIGINT,&siginth,0);

	//Create and bind the server socket. The following piece is very generic,
	//see the linux addrinfo manpages for information.
	int s = 0; //socket return values

	struct addrinfo h; //hints
	memset(&h,0,sizeof(struct addrinfo));
	h.ai_family = AF_UNSPEC; //IPv4 or IPv6 both acceptable
	h.ai_socktype = SOCK_STREAM; //TCP socket
	h.ai_flags = AI_PASSIVE;

	struct addrinfo *pr, *pq; //result
	s = getaddrinfo(0,"7020",&h,&pr); //note: port 80 (and < 1024 generally) requires root (or port redirection)
	if(s != 0){
		DebugPrintf("Error: getaddrinfo: %s\n",gai_strerror(s));
		return -1;
	}

	int sfd, efd; //socket file descriptors (server and epoll)
	for(pq = pr; pq != 0; pq = pq->ai_next){
		sfd = socket(pq->ai_family,pq->ai_socktype,pq->ai_protocol);
		if(sfd == -1)
			continue;
		//if(bind(sfd,pq->ai_addr,pq->ai_addrlen) == 0)
		s = bind(sfd,pq->ai_addr,pq->ai_addrlen);
		if(s == 0)
			break; //successfully bind
		printf("fail\n");
		close(sfd);
	}
	if(!pq){
		DebugPrintf("Error: Unable to bind.\n");
		return -1;
	}

	freeaddrinfo(pr);

	//Make the server socket non-blocking
	s = fcntl(sfd,F_SETFL,
		fcntl(sfd,F_GETFL,0)|O_NONBLOCK);
	if(s == -1)
		DebugPrintf("Warning: Unable to create a non-blocking socket.\n");

	//Finally host the server
	s = listen(sfd,SOMAXCONN);
	if(s == -1){
		DebugPrintf("Error: listen: %s\n",strerror(s));
		return -1;
	}

	//Create the fd monitoring instance
#define MAX_EVENTS 128
	struct epoll_event ev, events[MAX_EVENTS];
	efd = epoll_create1(0);
	if(efd == -1){
		DebugPrintf("Error: epoll_create\n");
		return -1;
	}

	ev.events = EPOLLIN|EPOLLET;
	ev.data.fd = sfd;
	s = epoll_ctl(efd,EPOLL_CTL_ADD,sfd,&ev);
	if(s == -1){
		DebugPrintf("Error: epoll_ctl\n");
		return -1;
	}

	//main server loop
	for(;;){
		int n = epoll_wait(efd,events,MAX_EVENTS,-1);
		if(n < 0)
			break;
		for(int i = 0; i < n; ++i){

			if(events[i].data.fd == sfd){
				//Event refers to server socket, meaning we have new incoming connection(s)
				struct sockaddr inaddr;
				uint l = sizeof(inaddr);
				for(;;){
					int cfd = accept4(sfd,&inaddr,&l,SOCK_NONBLOCK); //accept the connection, while also making it non-blocking
					if(cfd == -1)
						break;
					//identify the client
					char hostb[NI_MAXHOST], portb[NI_MAXHOST];
					getnameinfo(&inaddr,l,hostb,sizeof(hostb),portb,sizeof(portb),NI_NUMERICHOST|NI_NUMERICSERV);
					//TODO: create the client object
					DebugPrintf("fd = %u, %s:%s\n",cfd,hostb,portb);
				}
			}else{
				//
			}
		}
	}

	return 0;
}
