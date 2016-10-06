#ifndef SOCKET_H
#define SOCKET_H

class ServerSocket{
public:
	ServerSocket();
	~ServerSocket();
	int Listen();
	int Accept();
	int sfd; //server socket file descriptor
};

#endif
