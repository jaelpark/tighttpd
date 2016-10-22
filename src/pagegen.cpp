#include "main.h"
#include "socket.h"
#include "protocol.h"
#include "pagegen.h"

#include <dirent.h>

namespace PageGen{

HTMLPage::HTMLPage(Protocol::StreamProtocolData *_psp) : psp(_psp){
	//
}

HTMLPage::~HTMLPage(){
	//
}

HTTPError::HTTPError(Protocol::StreamProtocolData *_psp) : HTMLPage(_psp){
	//
}

HTTPError::~HTTPError(){
	//
}

void HTTPError::Generate(Protocol::StreamProtocolHTTPresponse::STATUS status){
	char buffer1[4096];
	size_t len = snprintf(buffer1,sizeof(buffer1),"<!DOCTYPE html>\r\n"
		"<html><head><title>%s</title></head><body><h1>%s</h1><hr>tighttpd/0.1</body></html>\r\n",
		Protocol::StreamProtocolHTTPresponse::pstatstr[status],Protocol::StreamProtocolHTTPresponse::pstatstr[status]);
	psp->Append(buffer1,len);
}

HTTPListDir::HTTPListDir(Protocol::StreamProtocolData *_psp) : HTMLPage(_psp){
	//
}

HTTPListDir::~HTTPListDir(){
	//
}

bool HTTPListDir::Generate(const char *path, const char *uri){
	DIR *pdir = opendir(path);
	if(!pdir)
		return false; //shouldn't happen
	char buffer1[4096];
	size_t len;

	len = snprintf(buffer1,sizeof(buffer1),"<!DOCTYPE html>\r\n"
		"<html><head><title>%s</title></head><body><h1>%s</h1>",uri,uri);
	psp->Append(buffer1,len);

	for(struct dirent *pent = readdir(pdir); pent != 0; pent = readdir(pdir)){
		//stat()
		len = snprintf(buffer1,sizeof(buffer1),"<a href=\"%s\">%s</a><br/>",pent->d_name,pent->d_name); //TODO: links, last modified etc
		psp->Append(buffer1,len);
	}

	closedir(pdir);

	len = snprintf(buffer1,sizeof(buffer1),"<hr>tighttpd/0.1</body></html>\r\n");
	psp->Append(buffer1,len);

	return true;
}

//

}
