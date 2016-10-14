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
	enum STATE{
		STATE_PENDING,
		STATE_SUCCESS, //required transfer complete
		STATE_CLOSED, //socket closed by remote
		STATE_CORRUPTED, //protocol violation
		STATE_ERROR //some internal error
	};
	Socket::ClientSocket socket;
	STATE state;
};

//general protocol interface which may later be used to expand functionality to other protocols such as FTP for server administration
class ClientProtocol{
public:
	ClientProtocol(Socket::ClientSocket, uint);
	virtual ~ClientProtocol();
	void * operator new(std::size_t);
	void operator delete(void *);
	inline StreamProtocol * GetStream() const{return psp;}
	inline Socket::ClientSocket GetSocket() const{return socket;}
	inline uint GetFlags() const{return sflags;}
	enum POLL{
		POLL_SKIP,
		POLL_RUN,
		POLL_CLOSE
	};
	//Allows protocol implementation to do (quick) work once read/write finishes. Return true if there's intensive work for the queue.
	virtual POLL Poll(uint) = 0;
	//Run() method for parallel execution
	virtual void Run() = 0;
	//virtual void RunConfig() = 0;
	//virtual void RunFinal() = 0;
	//
protected:
	StreamProtocol *psp;
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
	//
	enum STATUS{
		STATUS_200, //OK
		STATUS_304, //not modified,
		STATUS_400, //bad request
		STATUS_403, //forbidden
		STATUS_404, //not found
		STATUS_413, //request entity too large
		STATUS_500, //internal server error
		STATUS_501, //not implemented
		STATUS_505, //HTTP version not supported
	};
	void Initialize(STATUS);
	void AddHeader(const char *, const char *);
	void FormatHeader(const char *, const char *, ...);
	void Finalize();
	std::deque<char, tbb::cache_aligned_allocator<char>> buffer;
};

class StreamProtocolData : public StreamProtocol{
public:
	StreamProtocolData(Socket::ClientSocket);
	~StreamProtocolData();
	//data io
	bool Read();
	bool Write();
	void Reset();
	//
	void Append(const char *, size_t);
	std::deque<char, tbb::cache_aligned_allocator<char>> buffer;
};

class StreamProtocolFile : public StreamProtocol{
public:
	StreamProtocolFile(Socket::ClientSocket);
	~StreamProtocolFile();
	bool Read();
	bool Write();
	void Reset();
	//
	bool Open(const char *);
	FILE *pf;
	size_t len;
};

//class StreamProtocolFile

class ClientProtocolHTTP : public ClientProtocol{
public:
	ClientProtocolHTTP(Socket::ClientSocket);
	~ClientProtocolHTTP();
	StreamProtocol * GetStream() const;
	POLL Poll(uint);
	void Run();
protected:
	StreamProtocolHTTPrequest spreq;
	StreamProtocolHTTPresponse spres;
	StreamProtocolData spdata;
	StreamProtocolFile spfile;
	//TODO: timeout timer
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

	enum CONTENT{
		CONTENT_NONE,
		CONTENT_DATA,
		CONTENT_FILE
	} content;

public:
	static bool InitConfigModule(PyObject *, const char *);
	//static PyObject * Py_get(PyObject *, PyObject *);
	static PyObject *psub;
	static PyObject *pycode;
	static PyObject *pyglb;
	//
	//static PyObject *pyaddr;
};

}

#endif
