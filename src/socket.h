#ifndef SOCKET_H
#define SOCKET_H

namespace Socket{

class ServerSocket{
public:
	ServerSocket(int);
	ServerSocket(const ServerSocket &);
	~ServerSocket();
	int Listen();
	int Accept();
	int fd; //socket file descriptor
};

class ClientSocket{
public:
	ClientSocket(int);
	ClientSocket(const ClientSocket &);
	~ClientSocket();
	//int Connect(...);
	int Recv(void *, size_t);
	int Send(void *, size_t);
	int fd;
	//peername?
};

}

#endif
