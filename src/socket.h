#ifndef SOCKET_H
#define SOCKET_H

namespace Socket{

class ServerSocket{
public:
	ServerSocket(int);
	~ServerSocket();
	int Listen();
	int Accept();
	int sfd; //server socket file descriptor
};

class ClientSocket{
public:
	ClientSocket(int);
	~ClientSocket();
	//int Connect(...);
	int cfd;
};

}

#endif
