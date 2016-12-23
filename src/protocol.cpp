#include "main.h"
#include "socket.h"
#include "protocol.h"
#include "pagegen.h"

#include <stdarg.h> //FormatHeader()
#include <time.h> //date header field, connection timeout
#include <sys/stat.h> //file stats

#include <csignal>
#include <stdlib.h> //kill
#include <fcntl.h> //non-blocking pipe

#include <tbb/scalable_allocator.h>
#include <tbb/cache_aligned_allocator.h>

#include <string>
#include <sstream>

namespace Protocol{

StreamProtocol::StreamProtocol(Socket::ClientSocket _socket) : socket(_socket), state(STATE_PENDING){
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

void * ClientProtocol::operator new(std::size_t len){
	return scalable_malloc(len);
}

void ClientProtocol::operator delete(void *p){
	scalable_free(p);
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
	char buffer1[4096];
	ssize_t len = socket.Recv(buffer1,sizeof(buffer1));

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		state = STATE_CLOSED; //Some error occurred or socket was closed
		return true;
	}

	if(buffer.size()+len > 50000){
		state = STATE_CORRUPTED;
		return true; //too long, HTTP 413
	}

	buffer.insert(buffer.end(),buffer1,buffer1+len);
	if(ClientProtocolHTTP::FindBreak(&buffer,&postl)){
		state = STATE_SUCCESS;
		return true;
	}

	return false;
}

bool StreamProtocolHTTPrequest::Write(){
	//nothing to send
	return false;
}

void StreamProtocolHTTPrequest::Reset(){
	state = STATE_PENDING;
	buffer.clear();
}

StreamProtocolHTTPresponse::StreamProtocolHTTPresponse(Socket::ClientSocket _socket) : StreamProtocol(_socket){
	//
}

StreamProtocolHTTPresponse::~StreamProtocolHTTPresponse(){
	//
}

bool StreamProtocolHTTPresponse::Read(){
	//nothing to recv
	return false;
}

bool StreamProtocolHTTPresponse::Write(){
	tbb_string spresstr(buffer.begin(),buffer.end());
	ssize_t res = spresstr.size();
	ssize_t len = socket.Send(spresstr.c_str(),res);
	printf("--------\n%s--------\n",spresstr.c_str()); //debug

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		state = STATE_CLOSED;
		return true;
	}

	if(len == res){
		state = STATE_SUCCESS;
		return true;
	}else buffer.erase(buffer.begin(),buffer.begin()+len);

	return false;
}

void StreamProtocolHTTPresponse::Reset(){
	state = STATE_PENDING;
	buffer.clear();
}

void StreamProtocolHTTPresponse::Generate(const char *pstatus, bool crlf){
	char buffer1[4096];
	size_t len;

	time_t rt;
	time(&rt);
	const struct tm *pti = gmtime(&rt);
#define DATEFMT_RFC1123 "%a, %d %b %Y %H:%M:%S %Z"
	len = strftime(buffer1,sizeof(buffer1),"Date: " DATEFMT_RFC1123 "\r\n",pti); //mandatory
	buffer.insert(buffer.begin(),buffer1,buffer1+len);

	len = snprintf(buffer1,sizeof(buffer1),"HTTP/1.1 %s\r\nServer: tighttpd/0.1\r\n",pstatus);
	buffer.insert(buffer.begin(),buffer1,buffer1+len);

	if(!crlf)
		return;

	static const char *pclrf = "\r\n";
	buffer.insert(buffer.end(),pclrf,pclrf+2);
}

void StreamProtocolHTTPresponse::Generate(STATUS status, bool crlf){
	Generate(pstatstr[status],crlf);
}

void StreamProtocolHTTPresponse::AddHeader(const char *pname, const char *pfield){
	char buffer1[4096];
	size_t len = snprintf(buffer1,sizeof(buffer1),"%s: %s\r\n",pname,pfield);
	buffer.insert(buffer.end(),buffer1,buffer1+len);
}

void StreamProtocolHTTPresponse::FormatHeader(const char *pname, const char *pfmt, ...){
	char buffer1[4096];
	va_list args;
	va_start(args,pfmt);
	vsnprintf(buffer1,sizeof(buffer1),pfmt,args);
	va_end(args);
	AddHeader(pname,buffer1);
}

void StreamProtocolHTTPresponse::FormatTime(const char *pname, time_t *prt){
	char buffer1[4096];
	const struct tm *pti = gmtime(prt);
	strftime(buffer1,sizeof(buffer1),DATEFMT_RFC1123,pti); //mandatory
	AddHeader(pname,buffer1);
}

const char *StreamProtocolHTTPresponse::pstatstr[StreamProtocolHTTPresponse::STATUS_COUNT] = {
	"200 OK",
	"303 See Other",
	"304 Not Modified",
	"400 Bad Request",
	"403 Forbidden",
	"404 Not Found",
	"405 Method Not Allowed",
	"411 Length Required",
	"413 Request Entity Too Large",
	"500 Internal Server Error",
	"501 Not Implemented",
	"505 HTTP Version Not Supported"
};

StreamProtocolData::StreamProtocolData(Socket::ClientSocket _socket) : StreamProtocol(_socket){
	//
}

StreamProtocolData::~StreamProtocolData(){
	//
}

bool StreamProtocolData::Write(){
	tbb_string spresstr(buffer.begin(),buffer.end());
	ssize_t res = spresstr.size();
	ssize_t len = socket.Send(spresstr.c_str(),res);

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		state = STATE_CLOSED;
		return true;
	}

	if(len == res){
		state = STATE_SUCCESS;
		return true;
	}else buffer.erase(buffer.begin(),buffer.begin()+len);

	return false;
}

bool StreamProtocolData::Read(){
	//
	return false;
}

void StreamProtocolData::Reset(){
	state = STATE_PENDING;
	buffer.clear();
}

void StreamProtocolData::Append(const char *pdata, size_t datal){
	buffer.insert(buffer.end(),pdata,pdata+datal);
}

StreamProtocolFile::StreamProtocolFile(Socket::ClientSocket _socket) : StreamProtocol(_socket), pf(0){
	//
}

StreamProtocolFile::~StreamProtocolFile(){
	//
}

bool StreamProtocolFile::Write(){
	char buffer1[4096];
	ssize_t res = fread(buffer1,1,sizeof(buffer1),pf);
	ssize_t len = socket.Send(buffer1,res);

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		state = STATE_CLOSED;
		return true;
	}

	if(len < res){
		fseek(pf,SEEK_CUR,len-res);
	}else
	if(feof(pf)){ //also, len == res
		state = STATE_SUCCESS;
		return true;
	}

	return false;
}

bool StreamProtocolFile::Read(){
	//
	return false;
}

void StreamProtocolFile::Reset(){
	state = STATE_PENDING;
	if(pf){
		fclose(pf);
		pf = 0;
	}
}

bool StreamProtocolFile::Open(const char *path){
	if(!(pf = fopen(path,"rb")))
		return false; //may not be sufficient check in case of directories
	fseek(pf,0,SEEK_END);
	len = ftell(pf);
	fseek(pf,0,SEEK_SET);

	return true;
}

StreamProtocolCgi::StreamProtocolCgi(Socket::ClientSocket _socket) : StreamProtocol(_socket), pid(0), feedback(false){
	//
}

StreamProtocolCgi::~StreamProtocolCgi(){
	//
}

bool StreamProtocolCgi::Write(){
	char buffer1[4096];
	errno = 0;
	ssize_t res = read(pipefdi[0],buffer1,sizeof(buffer1));
	if((res < 0 && errno != EAGAIN) || res == 0){
		state = STATE_SUCCESS;
		return true;
	}
	if((res < 0 && errno == EAGAIN)){
		struct timespec ts1;
		clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts1);
		long dt = ts1.tv_nsec-ts.tv_nsec;
		if(dt < 5e9)
			return false;
		state = STATE_SUCCESS;
		return true;
	}

	errno = 0;

	//
	if(!feedback){
		buffer.insert(buffer.end(),buffer1,buffer1+res);

		size_t postl;
		if(ClientProtocolHTTP::FindBreak(&buffer,&postl)){
			tbb_string feedbstr(buffer.begin(),buffer.begin()+postl), status;
			if(ClientProtocolHTTP::ParseHeader(0,"\r\n"+feedbstr,"Status",status))
				pres->Generate(status.c_str(),false);
			else pres->Generate(Protocol::StreamProtocolHTTPresponse::STATUS_200,false);

			buffer.insert(buffer.begin(),pres->buffer.begin(),pres->buffer.end());
			feedback = true;

			tbb_string spresstr(buffer.begin(),buffer.end());
		}
	}

	if(feedback){
		if(buffer.size() > 0){
			tbb_string spresstr(buffer.begin(),buffer.end());
			ssize_t len = socket.Send(spresstr.c_str(),spresstr.size());

			if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
				state = STATE_CLOSED;
				return true;
			}

			buffer.erase(buffer.begin(),buffer.begin()+len);
		}else{
			ssize_t len = socket.Send(buffer1,res);
			if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
				state = STATE_CLOSED;
				return true;
			}

			if(len < res)
				buffer.insert(buffer.end(),buffer1+len,buffer1+res);
		}
	}

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts);

	return false;
}

bool StreamProtocolCgi::Read(){
	char buffer1[4096];
	ssize_t len = socket.Recv(buffer1,sizeof(buffer1));

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		close(pipefdo[1]);
		state = STATE_CLOSED;
		return true;
	}

	datac += len;
	write(pipefdo[1],buffer1,len); //assume that pipe is available for writing

	if(datac >= datal){
		close(pipefdo[1]);
		state = STATE_SUCCESS;
		return true;
	}

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts);

	return false;
}

void StreamProtocolCgi::Reset(){
	state = STATE_PENDING;
	feedback = false;
	buffer.clear();
	envbuf.clear();
	envptr.clear();
	if(pid != 0){
		kill(pid,SIGINT);
		pid = 0;
		close(pipefdi[0]);
	}
}

void StreamProtocolCgi::AddEnvironmentVar(const char *pname, const char *pcontent){
	char buffer[4096];
	size_t l = snprintf(buffer,sizeof(buffer),"%s=%s",pname,pcontent);
	std::deque<char, tbb::cache_aligned_allocator<char>>::const_iterator m = envbuf.insert(envbuf.end(),buffer,buffer+l+1);
	envptr.push_back(&(*m));
}

bool StreamProtocolCgi::Open(const char *pcgi, size_t _datal, StreamProtocolHTTPrequest *pleak, StreamProtocolHTTPresponse *_pres){
	envptr.push_back(0);

	pipe(pipefdo);
	pipe(pipefdi);

	pid = fork();
	if(pid == 0){
		dup2(pipefdo[0],STDIN_FILENO); //0
		close(pipefdo[0]);
		close(pipefdo[1]);
		dup2(pipefdi[1],STDOUT_FILENO); //1
		close(pipefdi[0]);
		close(pipefdi[1]);

		execle("/usr/bin/php-cgi","php-cgi",0,envptr.data());
		//
		exit(0); //exit child in case of failure
	}

	close(pipefdo[0]);
	close(pipefdi[1]);

	fcntl(pipefdi[0],F_SETFL,fcntl(pipefdi[0],F_GETFL,0)|O_NONBLOCK);

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts);
	pres = _pres;
	datac = 0;
	datal = _datal;

	if(pleak->postl < pleak->buffer.size()){
		tbb_string leakstr(pleak->buffer.begin()+pleak->postl,pleak->buffer.end());
		datac += write(pipefdo[1],leakstr.c_str(),leakstr.size());
		close(pipefdo[1]);
	}

	return true;
}

ClientProtocolHTTP::ClientProtocolHTTP(Socket::ClientSocket _socket, ServerInterface *_psi) : ClientProtocol(_socket,PROTOCOL_RECV), psi(_psi), spreq(_socket), spres(_socket), spdata(_socket), spfile(_socket), spcgi(_socket){
	Reset();
}

ClientProtocolHTTP::~ClientProtocolHTTP(){
	//
	socket.Close();
}

ClientProtocol::POLL ClientProtocolHTTP::Poll(uint sflag1){
	//main HTTP state machine
	if(sflag1 == PROTOCOL_ACCEPT)
		return POLL_SKIP; //SM already initialized

	//no need to check sflags, since only either PROTOCOL_SEND or RECV is enabled according to current state
	if(state == STATE_RECV_REQUEST || state == STATE_RECV_DATA){
		sflags = 0; //do not expect traffic until request has been processed
		return POLL_RUN; //handle the request in parallel Run()

	}else
	if(state == STATE_SEND_RESPONSE){
		if(content == CONTENT_NONE){
			Clear();
			if(connection == CONNECTION_KEEPALIVE){
				Reset();
				return POLL_SKIP;
			}else return POLL_CLOSE;
		}

		switch(content){
		case CONTENT_DATA:
			psp = (StreamProtocolData*)&spdata;
			break;
		case CONTENT_FILE:
			psp = (StreamProtocolData*)&spfile;
			break;
		case CONTENT_CGI:
			psp = (StreamProtocolData*)&spcgi;
			break;
		}
		state = STATE_SEND_DATA;
		//sflags = PROTOCOL_SEND; //keep sending

		return POLL_SKIP;
	}else
	if(state == STATE_SEND_DATA){
		{
			Clear();
			if(connection == CONNECTION_KEEPALIVE){
				Reset();
				return POLL_SKIP;
			}else return POLL_CLOSE;
		}

		//return POLL_SKIP;
	}

	//default queries
	return POLL_SKIP;
}

bool ClientProtocolHTTP::Run(){
	//
	if(state == STATE_RECV_REQUEST){
		try{
			if(spreq.state == StreamProtocol::STATE_CORRUPTED)
				throw(StreamProtocolHTTPresponse::STATUS_413); //or 400
			else
			if(spreq.state == StreamProtocol::STATE_CLOSED)
				return false;

			tbb_string spreqstr(spreq.buffer.begin(),spreq.buffer.begin()+spreq.postl);

			//https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html (HTTP/1.1 request)
			//https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.5
			size_t lf = spreqstr.find("\r\n"); //Find the first CRLF. No newlines allowed in Request-Line
			tbb_string request = spreqstr.substr(0,lf);

			//parse the request line ----------------------------------------------------------------------------
			//https://www.w3.org/Protocols/rfc2616/rfc2616-sec9.html (methods)
			static const char *pmstr[] = {"HEAD","GET","POST"};
			static const uint ml[] = {5,4,5};
			if(request.compare(0,ml[0],"HEAD ") == 0)
				method = METHOD_HEAD;
			else
			if(request.compare(0,ml[1],"GET ") == 0)
				method = METHOD_GET;
			else
			if(request.compare(0,ml[2],"POST ") == 0)
				method = METHOD_POST;
			else{
				//
				throw(StreamProtocolHTTPresponse::STATUS_501);
			}

			size_t ru = request.find_first_not_of(" ",ml[method]);
			if(ru == std::string::npos || request[ru] != '/')
				throw(StreamProtocolHTTPresponse::STATUS_400);
			size_t rl = request.find(' ',ru+1);
			if(rl == std::string::npos)
				throw(StreamProtocolHTTPresponse::STATUS_400);
			size_t hv = request.find_first_not_of(" ",rl+1);
			if(hv == std::string::npos || request.compare(hv,8,"HTTP/1.1") != 0)
				throw(StreamProtocolHTTPresponse::STATUS_505);

			tbb_string requri_enc = request.substr(ru,rl-ru);
			size_t enclen = requri_enc.size();

			//https://tools.ietf.org/html/rfc3986#section-3 (syntax components)
			size_t qv = requri_enc.find('?',0); //find the beginning of the query part
			if(qv == std::string::npos)
				qv = enclen;

			psi->resource = "/";
			psi->resource.reserve(qv);
			for(uint i = 1; i < qv; ++i){
				if(requri_enc[i] == '%'){
					if(i >= qv-2)
						break;
					tbb_string enc = requri_enc.substr(i+1,2);
					ulong c = strtoul(enc.c_str(),0,16);
					if(c != 0 && c < 256)
						psi->resource += (char)c;
					i += 2;
				}else psi->resource += requri_enc[i];
			}

			//TODO: decode the query (after &-tokenizing)?
			//create also python dictionary for these
			/*tbb_istringstream iss(requri_enc);
			for(tbb_string tok; getline(iss,tok,'&');){
				//resource += tok;
				printf("--%s\n",tok.c_str());
			}*/

			//parse the relevant headers ------------------------------------------------------------------------

			psi->ResetConfig();

			//tbb_string host, referer, useragent;
			if(!ParseHeader(lf,spreqstr,"Host",psi->host))
				throw(StreamProtocolHTTPresponse::STATUS_400); //always required by the 1.1 standard
			ParseHeader(lf,spreqstr,"Referer",psi->referer);
			ParseHeader(lf,spreqstr,"User-Agent",psi->useragent);

			char address[256] = "0.0.0.0";
			socket.Identify(address,sizeof(address));

			tbb_string hcnt;
			if(ParseHeader(lf,spreqstr,"Connection",hcnt) && hcnt.compare(0,10,"keep-alive") == 0)
				connection = CONNECTION_KEEPALIVE;
			else connection = CONNECTION_CLOSE;

			psi->uri = requri_enc;
			psi->address = tbb_string(address);
			ParseHeader(lf,spreqstr,"Accept",psi->accept);
			ParseHeader(lf,spreqstr,"Accept-Encoding",psi->acceptenc);
			ParseHeader(lf,spreqstr,"Accept-Language",psi->acceptlan);

			psi->Accept();

			tbb_string locald = psi->root+psi->resource; //TODO: check the object type
			//tbb_string mimetype = tbb_string(psi->mimetype.c_str());
			bool index = psi->index;
			bool listing = psi->listing;
			bool cgi = psi->cgi;
			//cgibin

			if(cgi)
				connection = CONNECTION_CLOSE; //length of the content unknown
			else
			if(method == METHOD_POST){
				spres.AddHeader("Allow","GET,HEAD");
				throw(StreamProtocolHTTPresponse::STATUS_405); //only scripts shall accept POST
			}

			const char *path = locald.c_str(); //TODO: security checks (for example handle '..' etc)
			//https://tools.ietf.org/html/rfc3986#section-5.2.4 (dot segments)

			struct stat statbuf;
			if(stat(path,&statbuf) == -1)
				throw(StreamProtocolHTTPresponse::STATUS_404); //TODO: set psp to 404 file.

			time_t modsince = ~0;
			if(ParseHeader(lf,spreqstr,"If-Modified-Since",hcnt)){
				struct tm ti;
				strptime(hcnt.c_str(),DATEFMT_RFC1123,&ti); //Assuming RFC1123 date format
				modsince = timegm(&ti);
			}
			//also: If-Unmodified-Since for range requests

			//generate the response -----------------------------------------------------------------------------

			if(S_ISDIR(statbuf.st_mode)){
				//Forward directory requests with /[uri] to /[uri]/
				if(requri_enc.back() != '/'){
					requri_enc += '/';
					spres.FormatHeader("Location",requri_enc.c_str());
					throw(StreamProtocolHTTPresponse::STATUS_303);
				}

				if(index){
					static const char *pindex[] = {"index.html","index.htm","index.shtml","index.php","index.py","index.pl","index.cgi"};
					for(uint i = 0, n = sizeof(pindex)/sizeof(pindex[0]); i < n; ++i){
						tbb_string page = locald+pindex[i];
						if(stat(page.c_str(),&statbuf) != -1 && !S_ISDIR(statbuf.st_mode)){
							locald = page;
							break;
						}
					}
				}
			}

			spres.AddHeader("Connection",connection == CONNECTION_KEEPALIVE?"keep-alive":"close");

			if(S_ISDIR(statbuf.st_mode)){
				//index-file not present
				//list the contents of this dir, if enabled
				if(!listing)
					throw(StreamProtocolHTTPresponse::STATUS_404);

				PageGen::HTTPListDir listdir(&spdata);
				if(method != METHOD_HEAD){
					if(!listdir.Generate(locald.c_str(),psi->resource.c_str(),psi))
						throw(StreamProtocolHTTPresponse::STATUS_500);
					content = CONTENT_DATA;
				}

				spres.AddHeader("Content-Type","text/html");
				spres.FormatHeader("Content-Length","%lu",spdata.buffer.size());
				spres.Generate(StreamProtocolHTTPresponse::STATUS_200);

			}else
			if(cgi){
				//https://tools.ietf.org/html/rfc3875
				ulong contentl = 0;
				if(method == METHOD_POST){
					if(!ParseHeader(lf,spreqstr,"Content-Length",hcnt) || !StrToUl(hcnt.c_str(),contentl))
						throw(StreamProtocolHTTPresponse::STATUS_411);
					spcgi.AddEnvironmentVar("CONTENT_LENGTH",hcnt.c_str());
					if(!ParseHeader(lf,spreqstr,"Content-Type",hcnt))
						hcnt.clear();//throw(StreamProtocolHTTPresponse::STATUS_400);
					spcgi.AddEnvironmentVar("CONTENT_TYPE",hcnt.c_str());
				}

				//HTTP_COOKIE
				/*spcgi.AddEnvironmentVar("HTTP_HOST",host.c_str());
				spcgi.AddEnvironmentVar("HTTP_REFERER",referer.c_str());
				spcgi.AddEnvironmentVar("HTTP_USER_AGENT",useragent.c_str());*/

				spcgi.AddEnvironmentVar("REQUEST_METHOD",pmstr[method]);
				spcgi.AddEnvironmentVar("REQUEST_URI",requri_enc.c_str());

				spcgi.AddEnvironmentVar("GATEWAY_INTERFACE","CGI/1.1");
				spcgi.AddEnvironmentVar("REMOTE_ADDR",address);
				spcgi.AddEnvironmentVar("REMOTE_HOST",address);
				spcgi.AddEnvironmentVar("QUERY_STRING",qv != enclen?requri_enc.substr(qv+1).c_str():"");
				spcgi.AddEnvironmentVar("REDIRECT_STATUS","200");

				spcgi.AddEnvironmentVar("SERVER_NAME",psi->name.c_str());
				//spcgi.AddEnvironmentVar("SERVER_ADDR","localhost");
				//spcgi.AddEnvironmentVar("SERVER_PORT","8080");
				spcgi.AddEnvironmentVar("SERVER_PROTOCOL","HTTP/1.1");
				spcgi.AddEnvironmentVar("SERVER_SOFTWARE","tighttpd/0.1");

				spcgi.AddEnvironmentVar("SCRIPT_NAME",psi->resource.c_str());
				spcgi.AddEnvironmentVar("SCRIPT_FILENAME",path);
				spcgi.AddEnvironmentVar("DOCUMENT_ROOT",psi->root.c_str());
				{
					if(!spcgi.Open(path,contentl,&spreq,&spres))
						throw(StreamProtocolHTTPresponse::STATUS_500);
					content = CONTENT_CGI;
				}

				//delayed response generation: cgi class handles this after checking the status feedback
				//spres.Generate(StreamProtocolHTTPresponse::STATUS_200,false);

			}else
			if(modsince != statbuf.st_mtime){
				if(method != METHOD_HEAD){
					if(!spfile.Open(path))
						throw(StreamProtocolHTTPresponse::STATUS_500); //send 500 since the file was supposed to exist
					content = CONTENT_FILE;
				}

				spres.AddHeader("Content-Type",psi->mimetype.c_str());
				spres.FormatHeader("Content-Length","%lu",statbuf.st_size);
				spres.FormatTime("Last-Modified",&statbuf.st_mtime);
				spres.Generate(StreamProtocolHTTPresponse::STATUS_200);
			}else{
				//content = CONTENT_NONE;
				spres.Generate(StreamProtocolHTTPresponse::STATUS_304);
			}

			if(method == METHOD_POST && spcgi.datac < spcgi.datal){
				//Receive rest of the POST if request packet didn't leak it already
				psp = &spcgi;
				state = STATE_RECV_DATA;
				sflags = PROTOCOL_RECV; //re-enable EPOLLIN
			}else
			if(cgi){
				//In case of cgi, the response is integrated to content due to need to check the status feedback
				psp = &spcgi;
				state = STATE_SEND_DATA;
				sflags = PROTOCOL_SEND;
			}else{
				psp = &spres;
				state = STATE_SEND_RESPONSE;
				sflags = PROTOCOL_SEND; //switch to EPOLLOUT
			}

		}catch(Protocol::StreamProtocolHTTPresponse::STATUS status){

			if(status >= Protocol::StreamProtocolHTTPresponse::STATUS_400){
				PageGen::HTTPError errorpage(&spdata);
				errorpage.Generate(status,psi);

				spres.AddHeader("Content-Type","text/html");
				spres.FormatHeader("Content-Length","%lu",spdata.buffer.size());

				if(method != METHOD_HEAD)
					content = CONTENT_DATA;

			}else spres.AddHeader("Content-Length","0"); //required

			spres.AddHeader("Connection",connection == CONNECTION_KEEPALIVE?"keep-alive":"close"); //warning: possible double
			spres.Generate(status);

			//prepare the special/fail response
			{
				psp = &spres;
				state = STATE_SEND_RESPONSE;
				sflags = PROTOCOL_SEND;
			}
		}
	}else
	if(state == STATE_RECV_DATA){
		if(spcgi.state == StreamProtocol::STATE_CLOSED)
			return false;

		psp = &spcgi;
		state = STATE_SEND_DATA;
		sflags = PROTOCOL_SEND;
	}

	return true;
}

void ClientProtocolHTTP::Reset(){
	//Reset the client to the state of post-accept
	psp = &spreq;
	state = STATE_RECV_REQUEST;
	sflags = PROTOCOL_RECV;

	method = METHOD_GET;
	content = CONTENT_NONE;
	connection = CONNECTION_CLOSE;
}

void ClientProtocolHTTP::Clear(){
	//Clear the buffers and close the files
	spreq.Reset();
	spres.Reset();
	spdata.Reset();
	spfile.Reset();
	spcgi.Reset();
}

bool ClientProtocolHTTP::ParseHeader(size_t lf, const tbb_string &spreqstr, const tbb_string &header, tbb_string &content){
	size_t hs = spreqstr.find("\r\n"+header+": ",lf);
	if(hs == std::string::npos)
		return false;
	size_t hl = hs+header.length()+4;
	size_t he = spreqstr.find("\r\n",hl);
	size_t hc = spreqstr.find_first_not_of(" ",hl);
	if(he <= hc)
		return false;
	content = spreqstr.substr(hc,he-hc);
	return true;
}

bool ClientProtocolHTTP::StrToUl(const char *pstr, ulong &ul){
	char *pendptr = 0;
	errno = 0;

	ul = strtoul(pstr,&pendptr,10);
	return !(errno == ERANGE || *pendptr != 0 || pstr == pendptr);
}

bool ClientProtocolHTTP::FindBreak(const std::deque<char, tbb::cache_aligned_allocator<char>> *pbuffer, size_t *pp){
	for(uint i = 0, n = pbuffer->size()-3; i < n; ++i){
		if((*pbuffer)[i+0] == '\r' && (*pbuffer)[i+1] == '\n' &&
			(*pbuffer)[i+2] == '\r' && (*pbuffer)[i+3] == '\n'){
			*pp = i+4;
			return true;
		}
	}
	return false;
}

}
