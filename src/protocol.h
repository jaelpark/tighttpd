#ifndef PROTOCOL_H
#define PROTOCOL_H

namespace Protocol{

#define PROTOCOL_RECV 0x1
#define PROTOCOL_SEND 0x2

class ClientProtocol{
public:
	ClientProtocol(Socket::ClientSocket, uint);
	~ClientProtocol();
	Socket::ClientSocket socket;
	//send|recv state flags indicating the expected state according to protocol rules
	//epoll_ctl is controlled in a generic way using these flags
	uint sflags;
};

class ClientProtocolHTTP : public ClientProtocol{
public:
	ClientProtocolHTTP(Socket::ClientSocket);
	~ClientProtocolHTTP();
	enum STATE{
		STATE_RECV_REQUEST,
		STATE_RECV_DATA,
		STATE_SEND_RESPONSE,
		STATE_SEND_DATA
	} state;
};

}

#endif
