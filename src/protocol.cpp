#include "main.h"
#include "socket.h"
#include "protocol.h"

namespace Protocol{

ClientProtocol::ClientProtocol(Socket::ClientSocket _socket) : socket(_socket){
	//
}

ClientProtocol::~ClientProtocol(){
	//
}

ClientProtocolHTTP::ClientProtocolHTTP(Socket::ClientSocket _socket) : ClientProtocol(_socket){
	//
	state = STATE_RECV_REQUEST;
}

ClientProtocolHTTP::~ClientProtocolHTTP(){
	//
}

}
