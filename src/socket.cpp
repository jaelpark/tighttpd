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

ServerSocket::ServerSocket(const ServerSocket &socket) : fd(socket.fd){
	//
}

ServerSocket::~ServerSocket(){
	//
}

int ServerSocket::Listen(const char *pport){
	//Create and bind the server socket. The following piece is very generic,
	//see the linux getaddrinfo man pages for information.

	struct addrinfo h; //hints
	memset(&h,0,sizeof(struct addrinfo));
	h.ai_family = AF_UNSPEC; //IPv4 or IPv6 both acceptable
	h.ai_socktype = SOCK_STREAM; //TCP socket
	h.ai_flags = AI_PASSIVE;

	struct addrinfo *pr, *pq; //result
	int s = getaddrinfo(0,pport,&h,&pr); //note: port 80 (and < 1024 generally) requires root (or port redirection)
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

int ServerSocket::Accept() const{
	struct sockaddr inaddr;
	uint l = sizeof(inaddr);

	int cfd = accept4(fd,&inaddr,&l,SOCK_NONBLOCK); //accept the connection, while also making it non-blocking

	return cfd;
}

void ServerSocket::Close() const{
	close(fd);
}

ClientSocket::ClientSocket(int socket) : fd(socket){
	//
}

ClientSocket::ClientSocket(const ClientSocket &socket) : fd(socket.fd){
	//
}

ClientSocket::~ClientSocket(){
	//
}

ssize_t ClientSocket::Recv(void *pbuf, size_t bufl) const{
	return recv(fd,pbuf,bufl,MSG_DONTWAIT);
}

ssize_t ClientSocket::Send(const void *pbuf, size_t bufl) const{
	return send(fd,pbuf,bufl,0);
}

void ClientSocket::Close() const{
	close(fd);
}

bool ClientSocket::Identify(char *paddr, size_t addrl) const{
	struct sockaddr c;
	socklen_t len = sizeof(c);
	return getnameinfo((struct sockaddr *)&c,len,paddr,addrl,0,0,NI_NUMERICHOST) == 0;
}

}
