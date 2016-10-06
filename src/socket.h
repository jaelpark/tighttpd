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
	enum STATE{
		STATE_DORMANT, //Expect no network traffic. Data won't be read. On level-triggered mode, epoll will renotify.
		STATE_RECV, //Receive data and write it directly to the output stream until it says to stop.
		STATE_SEND //Send data read from the input stream. Ignore notifications on incoming data until it's time to read again.
	} state;
	//peername?
};

}

#endif
