#ifndef SOCKET_H
#define SOCKET_H

namespace Socket{

class ServerSocket{
public:
	ServerSocket(int);
	ServerSocket(const ServerSocket &);
	~ServerSocket();
	int Listen(const char *);
	int Accept() const;
	void Close() const;
	int fd; //socket file descriptor
};

class ClientSocket{
public:
	ClientSocket(int);
	ClientSocket(const ClientSocket &);
	~ClientSocket();
	//int Connect(...);
	size_t Recv(void *, size_t) const;
	size_t Send(const void *, size_t) const;
	void Close() const;
	bool Identify(char *, size_t) const;
	int fd;
	//peername?
};

}

#endif
