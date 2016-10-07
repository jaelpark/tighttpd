#ifndef PROTOCOL_H
#define PROTOCOL_H

namespace Protocol{

#define PROTOCOL_RECV 0x1
#define PROTOCOL_SEND 0x2
#define PROTOCOL_ACCEPT 0x4

class StreamProtocol{
public:
	StreamProtocol(Socket::ClientSocket);
	~StreamProtocol();
	virtual bool Read() = 0;
	virtual bool Write() = 0;
	Socket::ClientSocket socket;
};

//general protocol interface which may later be used to expand functionality to other protocols such as FTP for server administration
class ClientProtocol{
public:
	ClientProtocol(Socket::ClientSocket, uint);
	~ClientProtocol();
	//Allows protocol implementation to do (quick) work once read/write finishes. Return true if there's intensive work for the queue.
	virtual bool Poll(uint) = 0;
	//Run() method for parallel execution
	virtual void Run() = 0;
	Socket::ClientSocket socket;
	//send|recv state flags indicating the expected state according to protocol rules
	//epoll_ctl is controlled in a generic way using these flags
	uint sflags;
};

class StreamProtocolHTTPrequest : public StreamProtocol{
public:
	StreamProtocolHTTPrequest(Socket::ClientSocket);
	~StreamProtocolHTTPrequest();
	bool Read();
	bool Write();
};

class ClientProtocolHTTP : public ClientProtocol{
public:
	ClientProtocolHTTP(Socket::ClientSocket);
	~ClientProtocolHTTP();
	bool Poll(uint);
	void Run();
	StreamProtocol *psp;
	StreamProtocolHTTPrequest spreq;
	enum STATE{
		STATE_RECV_REQUEST,
		STATE_RECV_DATA,
		STATE_SEND_RESPONSE,
		STATE_SEND_DATA
	} state;
};

}

#endif
