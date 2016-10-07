#include "main.h"
#include "socket.h"
#include "protocol.h"

#include <tbb/scalable_allocator.h>
#include <tbb/cache_aligned_allocator.h>

namespace Protocol{

StreamProtocol::StreamProtocol(Socket::ClientSocket _socket) : socket(_socket){
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
	int len = socket.Recv(buffer1,sizeof(buffer1));

	if(len > 0){
		//TODO: check the size limits
		buffer.insert(buffer.end(),buffer1,buffer1+len);
		if(strstr(buffer1,"\n\n") || strstr(buffer1,"\r\n\r\n"))
			return true;
	}

	return false;
}

bool StreamProtocolHTTPrequest::Write(){
	//nothing to send
	return false;
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
			sflags = 0;
			return true;
		}
	}

	return false;
}

void ClientProtocolHTTP::Run(){
	//
}

}
