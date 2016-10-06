#ifndef PROTOCOL_H
#define PROTOCOL_H

namespace Protocol{

class ClientProtocol{
public:
	ClientProtocol(Socket::ClientSocket);
	~ClientProtocol();
	Socket::ClientSocket socket;
};

class ClientProtocolHTTP : public ClientProtocol{
public:
	ClientProtocolHTTP(Socket::ClientSocket);
	~ClientProtocolHTTP();
	//InStream
	//OutStream
	enum STATE{
		STATE_RECV_REQUEST,
		STATE_RECV_DATA,
		STATE_SEND_RESPONSE,
		STATE_SEND_DATA
	} state;
};

}

#endif
