#include "main.h"
#include "socket.h"

#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>

namespace Socket{

ServerSocket::ServerSocket(int socket) : fd(socket){
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
	int s = getaddrinfo(0,"8080",&h,&pr); //note: port 80 (and < 1024 generally) requires root (or port redirection)
	if(s != 0){
		DebugPrintf("Error: getaddrinfo: %s\n",gai_strerror(s));
		return -1;
	}

	for(pq = pr; pq != 0; pq = pq->ai_next){
		fd = socket(pq->ai_family,pq->ai_socktype,pq->ai_protocol);
		if(fd == -1)
			continue;
		s = bind(fd,pq->ai_addr,pq->ai_addrlen);
		if(s == 0)
			break; //successfully bind
		printf("fail\n");
		close(fd);
	}
	if(!pq){
		DebugPrintf("Error: Unable to bind.\n");
		return -1;
	}

	freeaddrinfo(pr);

	//Make the server socket non-blocking
	s = fcntl(fd,F_SETFL,
		fcntl(fd,F_GETFL,0)|O_NONBLOCK);
	if(s == -1)
		DebugPrintf("Warning: Unable to create a non-blocking socket.\n");

	//Finally host the server
	s = listen(fd,SOMAXCONN);
	if(s == -1){
		DebugPrintf("Error: listen: %s\n",strerror(s));
		return -1;
	}

	return fd;
}

int ServerSocket::Accept(){
	struct sockaddr inaddr;
	uint l = sizeof(inaddr);

	int cfd = accept4(fd,&inaddr,&l,SOCK_NONBLOCK); //accept the connection, while also making it non-blocking

	return cfd;
}

ClientSocket::ClientSocket(int socket) : fd(socket){
	//
}

ClientSocket::ClientSocket(ClientSocket &socket) : fd(socket.fd){
	//
}

ClientSocket::~ClientSocket(){
	//
}

}
