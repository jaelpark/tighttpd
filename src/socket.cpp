#include "main.h"
#include "socket.h"

#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>

namespace Socket{

ServerSocket::ServerSocket(int socket) : sfd(socket){
	//
}

ServerSocket::~ServerSocket(){
	//
}

int ServerSocket::Listen(){
	//Create and bind the server socket. The following piece is very generic,
	//see the linux getaddrinfo manpages for information.

	struct addrinfo h; //hints
	memset(&h,0,sizeof(struct addrinfo));
	h.ai_family = AF_UNSPEC; //IPv4 or IPv6 both acceptable
	h.ai_socktype = SOCK_STREAM; //TCP socket
	h.ai_flags = AI_PASSIVE;

	struct addrinfo *pr, *pq; //result
	int s = getaddrinfo(0,"7020",&h,&pr); //note: port 80 (and < 1024 generally) requires root (or port redirection)
	if(s != 0){
		DebugPrintf("Error: getaddrinfo: %s\n",gai_strerror(s));
		return -1;
	}

	for(pq = pr; pq != 0; pq = pq->ai_next){
		sfd = socket(pq->ai_family,pq->ai_socktype,pq->ai_protocol);
		if(sfd == -1)
			continue;
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

	return sfd;
}

int ServerSocket::Accept(){
	struct sockaddr inaddr;
	uint l = sizeof(inaddr);

	int cfd = accept4(sfd,&inaddr,&l,SOCK_NONBLOCK); //accept the connection, while also making it non-blocking

	/*char hostb[NI_MAXHOST], portb[NI_MAXSERV];
	getnameinfo(&inaddr,l,hostb,sizeof(hostb),portb,sizeof(portb),
		NI_NUMERICHOST|NI_NUMERICSERV);

	DebugPrintf("fd = %u, %s:%s\n",cfd,hostb,portb);*/

	return cfd;
}

}
