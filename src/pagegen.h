#ifndef PAGEGEN_H
#define PAGEGEN_H

namespace PageGen{

class HTMLPage{
public:
	HTMLPage(Protocol::StreamProtocolData *);
	~HTMLPage();
	Protocol::StreamProtocolData *psp;
};

//Error HTML-page generator (for codes starting from 400+), if not using any user-specified ones
class HTTPError : public HTMLPage{
public:
	HTTPError(Protocol::StreamProtocolData *);
	~HTTPError();
	void Generate(Protocol::StreamProtocolHTTPresponse::STATUS);
};

class HTTPListDir : public HTMLPage{
public:
	HTTPListDir();
	~HTTPListDir();
};

}

#endif
