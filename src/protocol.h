/*
Copyright 2017 Jasper Parkkila

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list
   of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list
   of conditions and the following disclaimer in the documentation and/or other materials
   provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be
   used to endorse or promote products derived from this software without specific
   prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
HTTP implementation.
2.4.2017
*/

namespace Protocol{

#define PROTOCOL_RECV 0x1
#define PROTOCOL_SEND 0x2
#define PROTOCOL_ACCEPT 0x4

//Communication interface for specific data sources (file, data, pipe) for sending and targets for receiving. Implementations should also
//handle rules determining when the data transfer is complete, for example.
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
		POLL_SKIP, //no special operation
		POLL_RUN, //parallel offload
		POLL_CLOSE //close the socket, remove the client
	};
	//Allows protocol implementation to do (quick) work once read/write finishes. Return true if there's intensive work for the queue.
	virtual POLL Poll(uint) = 0;
	//
	virtual void Accept() = 0;
	virtual void Configure() = 0;
	virtual void Process() = 0;
	//
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
	bool CheckPipeline();
	//
	enum METHOD{
		METHOD_HEAD,
		METHOD_GET,
		METHOD_POST,
		METHOD_COUNT, //just the total number
	};
	std::deque<char, tbb::cache_aligned_allocator<char>> buffer;
	size_t postl; //POST leaking pointer, basically the first double-CRLF in the request
	static const char *pmethodstr[METHOD_COUNT];
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
		STATUS_303, //see other
		STATUS_304, //not modified,
		STATUS_400, //bad request
		STATUS_403, //forbidden
		STATUS_404, //not found
		STATUS_405, //method not allowed
		STATUS_411, //length required
		STATUS_413, //request entity too large
		STATUS_500, //internal server error
		STATUS_501, //not implemented
		STATUS_505, //HTTP version not supported
		STATUS_COUNT
	};
	void Generate(const char *, bool = true);
	void Generate(STATUS, bool = true);
	void AddHeader(const char *, const char *);
	void FormatHeader(const char *, const char *, ...);
	void FormatTime(const char *, time_t *);
	std::deque<char, tbb::cache_aligned_allocator<char>> buffer;
	static const char *pstatstr[STATUS_COUNT];
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

class StreamProtocolCgi : public StreamProtocol{
public:
	StreamProtocolCgi(Socket::ClientSocket);
	~StreamProtocolCgi();
	bool Read();
	bool Write();
	void Reset();
	//
	void AddEnvironmentVar(const char *, const char *);
	bool Open(const char *, const char *, size_t, StreamProtocolHTTPrequest *, StreamProtocolHTTPresponse *);
	StreamProtocolHTTPrequest *preq;
	StreamProtocolHTTPresponse *pres;
	int pipefdo[2];
	int pipefdi[2];
	pid_t pid;
	std::deque<char, tbb::cache_aligned_allocator<char>> buffer; //pipe/socket io buffering
	std::deque<char, tbb::cache_aligned_allocator<char>> envbuf; //cgi environment variables
	std::vector<const char *> envptr; //environment string pointers for execle
	//POST data
	size_t datal; //client Content-Length
	size_t datac; //POST pointer
	bool feedback; //cgi state feedback read
	bool empty; //cgi fully read
	struct timespec ts; //cgi timeout counter
};

class ClientProtocolHTTP : public ClientProtocol{
public:
	ClientProtocolHTTP(Socket::ClientSocket, ServerInterface *, ClientInterface *, boost::python::object);
	~ClientProtocolHTTP();
	StreamProtocol * GetStream() const;
	POLL Poll(uint);
	//
	void Accept();
	void Configure();
	void Process();
	//
protected:
	void Reset();
	void Clear();
	//
	ServerInterface *psi;
	ClientInterface *pci;
	boost::python::object clobj; //this is here just to keep the class object alive
	//
	StreamProtocolHTTPrequest spreq;
	StreamProtocolHTTPresponse spres;
	StreamProtocolData spdata;
	StreamProtocolFile spfile;
	StreamProtocolCgi spcgi;
	//TODO: timeout timer

	//Some state variables shared between Accept/Configure/Process
	StreamProtocolHTTPresponse::STATUS status;
	tbb_string spreqstr; //whole request buffer given by the Accept() here
	tbb_string requri_enc; //encoded request uri
	size_t enclen; //encoded request uri length
	size_t lf; //End position of the first line in the request
	size_t qv; //Query string position in the encoded uri
	//
	enum STATE{
		STATE_RECV_REQUEST,
		STATE_RECV_DATA,
		STATE_SEND_RESPONSE,
		STATE_SEND_DATA,
	} state;

	StreamProtocolHTTPrequest::METHOD method;

	enum CONNECTION{
		CONNECTION_CLOSE,
		CONNECTION_KEEPALIVE
	} connection; //persistent or non-persistent connection

	enum CONTENT{
		CONTENT_NONE,
		CONTENT_DATA,
		CONTENT_FILE,
		CONTENT_CGI
	} content; //type of the requested content

public:
	//utils
	static void ParseSegments(tbb_string &, tbb_string &);
	static bool ParseHeader(size_t, const tbb_string &, const tbb_string &, tbb_string &);
	static bool StrToUl(const char *, ulong &);
	static bool FindBreak(const std::deque<char, tbb::cache_aligned_allocator<char>> *, size_t *); //find double crlf
};

}

#endif
