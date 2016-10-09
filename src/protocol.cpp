#include "main.h"
#include "socket.h"
#include "protocol.h"

#include <time.h> //date header field

#include <tbb/scalable_allocator.h>
#include <tbb/cache_aligned_allocator.h>

namespace Protocol{

StreamProtocol::StreamProtocol(Socket::ClientSocket _socket) : socket(_socket), state(STATE_PENDING){
	//
}

StreamProtocol::~StreamProtocol(){
	//
}

ClientProtocol::ClientProtocol(Socket::ClientSocket _socket, uint _sflags) : socket(_socket), sflags(_sflags){
	//
}

ClientProtocol::~ClientProtocol(){
	//
}

void * ClientProtocol::operator new(std::size_t len){
	return scalable_malloc(len);
}

void ClientProtocol::operator delete(void *p){
	scalable_free(p);
}

//----------------------------------------------------------------
//HTTP implementation

StreamProtocolHTTPrequest::StreamProtocolHTTPrequest(Socket::ClientSocket _socket) : StreamProtocol(_socket){
	//
}

StreamProtocolHTTPrequest::~StreamProtocolHTTPrequest(){
	//
}

bool StreamProtocolHTTPrequest::Read(){
	char buffer1[4096];
	size_t len = socket.Recv(buffer1,sizeof(buffer1));

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		state = STATE_CLOSED; //Some error occurred or socket was closed
		return true;
	}

	size_t req = strlen(buffer1);
	if(req < len || buffer.size()+len > 50000){
		state = STATE_CORRUPTED;
		return true; //HTTP 413
	}

	buffer.insert(buffer.end(),buffer1,buffer1+len);
	//Assume that at least "Host:\r\n" is given, as it should be. This makes two CRLFs.
	if(strstr(buffer1,"\r\n\r\n")){
		state = STATE_SUCCESS;
		return true;
	}

	return false;
}

bool StreamProtocolHTTPrequest::Write(){
	//nothing to send
	return false;
}

void StreamProtocolHTTPrequest::Reset(){
	state = STATE_PENDING;
	buffer.clear();
}

StreamProtocolHTTPresponse::StreamProtocolHTTPresponse(Socket::ClientSocket _socket) : StreamProtocol(_socket){
	//
}

StreamProtocolHTTPresponse::~StreamProtocolHTTPresponse(){
	//
}

bool StreamProtocolHTTPresponse::Read(){
	//nothing to recv
	return false;
}

bool StreamProtocolHTTPresponse::Write(){
	std::basic_string<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>>
		spresstr(buffer.begin(),buffer.end());
	size_t res = spresstr.size();
	size_t len = socket.Send(spresstr.c_str(),res);

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		state = STATE_CLOSED;
		return true;
	}

	if(len == res){
		state = STATE_SUCCESS;
		return true;
	}else buffer.erase(buffer.begin(),buffer.begin()+len);

	return false;
}

void StreamProtocolHTTPresponse::Reset(){
	state = STATE_PENDING;
	buffer.clear();
}

void StreamProtocolHTTPresponse::Initialize(STATUS status){
	static const char *pstatstr[] = {
		"200 OK",
		"304 Not Found",
		"400 Bad Request",
		"403 Forbidden",
		"404 Not Found",
		"413 Request Entity Too Large",
		"500 Internal Server Error",
		"501 Not Implemented",
		"505 HTTP Version Not Supported"
	};

	char buffer1[4096];
	size_t len;

	len = snprintf(buffer1,sizeof(buffer1),"HTTP/1.1 %s\r\nServer: tighttpd/0.1\r\n",pstatstr[status]);
	buffer.insert(buffer.end(),buffer1,buffer1+len);

	time_t rt;
	time(&rt);
	const struct tm *pti = gmtime(&rt);
	len = strftime(buffer1,sizeof(buffer1),"Date: %a, %d %b %Y %H:%M:%S %Z\r\n",pti); //mandatory
	buffer.insert(buffer.end(),buffer1,buffer1+len);
}

void StreamProtocolHTTPresponse::AddHeader(const char *pname, const char *pfield){
	char buffer1[4096];
	size_t len = snprintf(buffer1,sizeof(buffer1),"%s: %s\r\n",pname,pfield);
	buffer.insert(buffer.end(),buffer1,buffer1+len);
}

void StreamProtocolHTTPresponse::Finalize(){
	static const char *pclrf = "\r\n";
	buffer.insert(buffer.end(),pclrf,pclrf+2);
}

StreamProtocolData::StreamProtocolData(Socket::ClientSocket _socket) : StreamProtocol(_socket){
	//
}

StreamProtocolData::~StreamProtocolData(){
	//
}

bool StreamProtocolData::Write(){
	std::basic_string<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>>
		spresstr(buffer.begin(),buffer.end());
	size_t res = spresstr.size();
	size_t len = socket.Send(spresstr.c_str(),res);

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		state = STATE_CLOSED;
		return true;
	}

	if(len == res){
		state = STATE_SUCCESS;
		return true;
	}else buffer.erase(buffer.begin(),buffer.begin()+len);

	return false;
}

bool StreamProtocolData::Read(){
	//
	return false;
}

void StreamProtocolData::Reset(){
	state = STATE_PENDING;
	buffer.clear();
}

void StreamProtocolData::Append(const char *pdata, size_t datal){
	buffer.insert(buffer.end(),pdata,pdata+datal);
}

ClientProtocolHTTP::ClientProtocolHTTP(Socket::ClientSocket _socket) : ClientProtocol(_socket,PROTOCOL_RECV), spreq(_socket), spres(_socket), spdata(_socket){
	psp = &spreq;
	state = STATE_RECV_REQUEST;
}

ClientProtocolHTTP::~ClientProtocolHTTP(){
	//
	socket.Close();
}

ClientProtocol::POLL ClientProtocolHTTP::Poll(uint sflag1){
	if(sflag1 == PROTOCOL_ACCEPT){
		//Nothing to do, all the relevant flags have already been set. Awaiting for request.
		return POLL_SKIP;
	}

	if(state == STATE_RECV_REQUEST){
		//sflag1 == PROTOCOL_RECV
		sflags = 0; //do not expect traffic until request has been processed
		return POLL_RUN; //handle the request in Run()

	}else
	if(state == STATE_SEND_RESPONSE){
		if(method == METHOD_HEAD)
			return POLL_CLOSE;

		const char test[] = "Some content\r\n";
		spdata.Append(test,strlen(test));

		psp = &spdata;
		state = STATE_SEND_DATA;
		//sflags = PROTOCOL_SEND; //keep sending

		return POLL_SKIP;
	}else
	if(state == STATE_SEND_DATA){
		//
		return POLL_CLOSE;
	}

	return POLL_SKIP;
}

void ClientProtocolHTTP::Run(){
	//
	if(state == STATE_RECV_REQUEST){
		if(spreq.state == StreamProtocol::STATE_CORRUPTED){
			spres.Initialize(StreamProtocolHTTPresponse::STATUS_413); //or 400
			spres.Finalize();
			return;
		}else
		if(spreq.state == StreamProtocol::STATE_ERROR){
			spres.Initialize(StreamProtocolHTTPresponse::STATUS_500);
			spres.Finalize();
			return;
		}else
		if(spreq.state == StreamProtocol::STATE_CLOSED){
			//remove the client
			return;
		}

		std::basic_string<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>>
			spreqstr(spreq.buffer.begin(),spreq.buffer.end());
		//const char *pr = request.c_str();

		//https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html (HTTP/1.1 request)
		//https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.5
		size_t lf = spreqstr.find("\r\n"); //Find the first CRLF. No newlines allowed in Request-Line

		std::basic_string<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>>
			request = spreqstr.substr(0,lf);

		//https://www.w3.org/Protocols/rfc2616/rfc2616-sec9.html (methods)
		static const uint ml[] = {5,4,5};
		if(request.compare(0,ml[0],"HEAD ") == 0)
			method = METHOD_HEAD;
		else
		if(request.compare(0,ml[1],"GET ") == 0)
			method = METHOD_GET;
		else
		if(request.compare(0,ml[2],"POST ") == 0)
			method = METHOD_POST;
		else{
			spres.Initialize(StreamProtocolHTTPresponse::STATUS_501);
			spres.Finalize();
			return;
		}

		size_t ru = request.find('/',ml[method]);
		if(ru == -1 || request.find_first_not_of(" ",ml[method]) < ru){
			//TODO: support Absolute-URI
			spres.Initialize(StreamProtocolHTTPresponse::STATUS_400);
			spres.Finalize();
			return;
		}

		size_t rl = request.find(' ',ru+1);
		if(rl == -1){
			spres.Initialize(StreamProtocolHTTPresponse::STATUS_400);
			spres.Finalize();
			return;
		}

		size_t hv = request.find("HTTP/1.1",rl+1);
		if(hv == -1 || request.find_first_not_of(" ",rl+1) < hv){
			//HTTP 400 or 505
			spres.Initialize(StreamProtocolHTTPresponse::STATUS_505);
			spres.Finalize();
			return;
		}

		std::basic_string<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>>
			requri = request.substr(ru,rl-ru);

		printf("%s|\n",requri.c_str());
		//if keep-alive: spreq.Reset();

		//Get the file size or prepare StreamProtocolData and determine its final length.
		//Connection: keep-alive requires Content-Length

		spres.Initialize(StreamProtocolHTTPresponse::STATUS_200); //or 301
		spres.AddHeader("Connection","close");
		spres.Finalize();

		if(method == METHOD_POST){
			psp = &spdata;
			state = STATE_RECV_DATA;
			//
			sflags = PROTOCOL_RECV; //re-enable EPOLLIN
		}else{
			psp = &spres;
			state = STATE_SEND_RESPONSE;
			//
			sflags = PROTOCOL_SEND; //switch to EPOLLOUT
		}
	}
}

}
