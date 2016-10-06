#include "main.h"
#include "socket.h"
#include "protocol.h"

namespace Protocol{

ClientProtocol::ClientProtocol(Socket::ClientSocket *psocket){
	//
}

ClientProtocol::~ClientProtocol(){
	//
}

ClientHTTP::ClientHTTP(Socket::ClientSocket *psocket) : ClientProtocol(psocket){
	//
}

ClientHTTP::~ClientHTTP(){
	//
}

}
