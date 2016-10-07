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
	int fd;
	//peername?
};

}

#endif
