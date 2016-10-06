#ifndef PROTOCOL_H
#define PROTOCOL_H

namespace Socket{
	class ClientSocket;
};

namespace Protocol{

class ClientProtocol{
public:
	ClientProtocol(class Socket::ClientSocket *);
	~ClientProtocol();
};

class ClientHTTP : public ClientProtocol{
	ClientHTTP(class Socket::ClientSocket *);
	~ClientHTTP();
};

}

#endif
