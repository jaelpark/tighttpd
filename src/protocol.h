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
	virtual void Reset() = 0;
	Socket::ClientSocket socket;
	enum STATE{
		STATE_PENDING,
		STATE_SUCCESS, //required transfer complete
		STATE_CLOSED, //socket closed by remote
		STATE_CORRUPTED, //protocol violation
		STATE_ERROR //some internal error
	} state;
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
	void Reset();
	std::deque<char, tbb::cache_aligned_allocator<char>> buffer;
};

class StreamProtocolHTTPresponse : public StreamProtocol{
public:
	StreamProtocolHTTPresponse(Socket::ClientSocket);
	~StreamProtocolHTTPresponse();
	bool Read();
	bool Write();
	void Reset();
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

	enum METHOD{
		METHOD_HEAD,
		METHOD_GET,
		METHOD_POST
	} method;
};

}

#endif
