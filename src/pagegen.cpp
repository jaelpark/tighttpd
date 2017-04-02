/*
Copyright 2017 Jasper Parkkila

Redistribution and use in source and binary forms, with or without modification, are
permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list
   of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list
   of conditions and the following disclaimer in the documentation and/or other materials
   provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be
   used to endorse or promote products derived from this software without specific
   prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "main.h"
#include "socket.h"
#include "protocol.h"
#include "pagegen.h"

#include <dirent.h>

/*
Generation of error pages and directory listings.
2.4.2017
*/

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

void HTTPError::Generate(Protocol::StreamProtocolHTTPresponse::STATUS status, const ServerInterface *psi){
	char buffer1[4096];
	size_t len = snprintf(buffer1,sizeof(buffer1),"<!DOCTYPE html>\r\n"
		"<html><head><title>%s</title></head><body><h1>%s</h1><hr>%s</body></html>\r\n",
		Protocol::StreamProtocolHTTPresponse::pstatstr[status],Protocol::StreamProtocolHTTPresponse::pstatstr[status],psi->name.c_str());
	psp->Append(buffer1,len);
}

HTTPListDir::HTTPListDir(Protocol::StreamProtocolData *_psp) : HTMLPage(_psp){
	//
}

HTTPListDir::~HTTPListDir(){
	//
}

bool HTTPListDir::Generate(const char *path, const char *uri, const ServerInterface *psi){
	DIR *pdir = opendir(path);
	if(!pdir)
		return false; //shouldn't happen
	char buffer1[4096];
	size_t len;

	len = snprintf(buffer1,sizeof(buffer1),"<!DOCTYPE html>\r\n"
		"<html><head><title>%s</title></head><body><h1>%s</h1>"
		"<a href=\"..\">..</a><br/>",uri,uri);
	psp->Append(buffer1,len);

	//TODO: scandir to sort
	for(struct dirent *pent = readdir(pdir); pent != 0; pent = readdir(pdir)){
		//stat()
		if(strcmp(pent->d_name,".") == 0 || strcmp(pent->d_name,"..") == 0) //dot segments were manually added
			continue;
		//TODO: file icons, e.g. /res:icon?php.
		//css styling etc.
		len = snprintf(buffer1,sizeof(buffer1),"<a href=\"%s\">%s</a><br/>",pent->d_name,pent->d_name); //TODO: links, last modified etc
		psp->Append(buffer1,len);
	}

	closedir(pdir);

	len = snprintf(buffer1,sizeof(buffer1),"<hr>%s</body></html>\r\n",psi->name.c_str());
	psp->Append(buffer1,len);

	return true;
}

//

}
