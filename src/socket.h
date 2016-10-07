#ifndef SOCKET_H
#define SOCKET_H

namespace Socket{

class ServerSocket{
public:
	ServerSocket(int);
	~ServerSocket();
	int Listen();
	int Accept();
	int fd; //socket file descriptor
};

class ClientSocket{
public:
	ClientSocket(int);
	ClientSocket(ClientSocket &);
	~ClientSocket();
	//int Connect(...);
	int fd;
	//peername?
};

}

#endif
