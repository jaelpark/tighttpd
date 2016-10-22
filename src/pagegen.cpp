#include "main.h"
#include "socket.h"
#include "protocol.h"
#include "pagegen.h"

namespace PageGen{

HTMLPage::HTMLPage(Protocol::StreamProtocolData *_psp) : psp(_psp){
	//
}

HTMLPage::~HTMLPage(){
	//
}

HTTPError::HTTPError(Protocol::StreamProtocolData *_psp) : HTMLPage(psp){
	//
}

HTTPError::~HTTPError(){
	//
}

void HTTPError::Generate(Protocol::StreamProtocolHTTPresponse::STATUS status){
	char buffer1[4096];
	size_t len = snprintf(buffer1,sizeof(buffer1),"<!DOCTYPE html>\r\n\
		<html><head><title>%s</title></head><body><h1>%s</h1><hr>tighttpd/0.1</body></html>\r\n",
		Protocol::StreamProtocolHTTPresponse::pstatstr[status],Protocol::StreamProtocolHTTPresponse::pstatstr[status]);
	psp->Append(buffer1,len);
}

}
