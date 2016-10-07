#include "main.h"
#include "socket.h"
#include "protocol.h"

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

	if(len < 0){
		if(errno != EAGAIN && errno != EWOULDBLOCK){
			state = STATE_CLOSED;
			return true; //close the socket
		}
	}else
	if(len == 0){
		{
			state = STATE_CLOSED;
			return true;
		}
	}else{
		size_t req = strlen(buffer1);
		if(req < len || buffer.size()+len > 50000){
			state = STATE_CORRUPTED;
			return true; //HTTP 400
		}

		buffer.insert(buffer.end(),buffer1,buffer1+len);
		if(strstr(buffer1,"\r\n\r\n")){ //Assume that at least "Host:\r\n" is given, as it should be. This makes two CRLFs.
			state = STATE_SUCCESS;
			return true;
		}

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
	//TODO: send the generated response
	return false;
}

void StreamProtocolHTTPresponse::Reset(){
	//
}

ClientProtocolHTTP::ClientProtocolHTTP(Socket::ClientSocket _socket) : ClientProtocol(_socket,PROTOCOL_RECV), spreq(_socket){
	psp = &spreq;
	state = STATE_RECV_REQUEST;
}

ClientProtocolHTTP::~ClientProtocolHTTP(){
	//
}

bool ClientProtocolHTTP::Poll(uint sflag1){
	if(sflag1 == PROTOCOL_ACCEPT){
		//Nothing to do, all the relevant flags have already been set. Awaiting for request.
		return false;
	}

	if(state == STATE_RECV_REQUEST){
		if(sflag1 == PROTOCOL_RECV){
			//

			sflags = 0;
			return true;
		}
	}

	return false;
}

void ClientProtocolHTTP::Run(){
	//
	if(state == STATE_RECV_REQUEST){
		if(spreq.state == StreamProtocol::STATE_CORRUPTED){
			//HTTP 400
			return;
		}else
		if(spreq.state == StreamProtocol::STATE_ERROR){
			//HTTP 500
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
			//HTTP 501
			return;
		}

		size_t ru = request.find('/',ml[method]);
		if(ru == -1 || request.find_first_not_of(" ",ml[method]) < ru){
			//TODO: support Absolute-URI
			//HTTP 400
			return;
		}

		size_t rl = request.find(' ',ru+1);
		if(rl == -1){
			//HTTP 400
			return;
		}

		size_t hv = request.find("HTTP/1.1",rl+1);
		if(hv == -1 || request.find_first_not_of(" ",rl+1) < ru){
			//HTTP 400 or 505
			return;
		}

		std::basic_string<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>>
			requri = request.substr(ru,rl-ru);

		printf("|%s|\n",requri.c_str());
		//if keep-alive: spreq.Reset();
	}
}

}
