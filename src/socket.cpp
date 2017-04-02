/*
Copyright 2017 Jasper Parkkila

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list
   of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list
   of conditions and the following disclaimer in the documentation and/or other materials
   provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be
   used to endorse or promote products derived from this software without specific
   prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "main.h"
#include "socket.h"

#include <sys/socket.h>
#include <netdb.h>
#include <fcntl.h>
#include <unistd.h>

/*
Simple socket wrapper.
2.4.2017
*/

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
