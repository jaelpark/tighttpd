#include "main.h"
#include "socket.h"
#include "protocol.h"

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
	char buffer[4096];
	uint len = socket.Recv(buffer,sizeof(buffer));
	//TODO: append to buffer until \n\n or \r\n\r\n or size limit is reacheds

	//testing
	printf("%s",buffer);
	if(strstr(buffer,"\n\n") || strstr(buffer,"\r\n\r\n")){ //TODO: strstr(buffer+last_recv_size,...)
		printf("----------- DONE\n");
		return true;
	}
	return false;
}

bool StreamProtocolHTTPrequest::Write(){
	//nothing to send
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
