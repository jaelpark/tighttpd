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

/*
HTTP implementation.
2.4.2017
*/

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
		state = STATE_CLOSED; //Some error occurred or the socket was closed
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
	return false;
}

//Reset the request data to the state of a new client
void StreamProtocolHTTPrequest::Reset(){
	state = STATE_PENDING;
	//Remove the contents of the request up to the POST data or next request. This is to support pipelining properly.
	buffer.erase(buffer.begin(),buffer.begin()+postl);
}

//Upon calling Reset(), checks if there are pending (intact) requests in the buffer.
bool StreamProtocolHTTPrequest::CheckPipeline(){
	if(buffer.size() >= 4 && ClientProtocolHTTP::FindBreak(&buffer,&postl)){
		state = STATE_SUCCESS; //Request has been received, we can skip that part next time
		return true;
	}

	return false;
}

const char *StreamProtocolHTTPrequest::pmethodstr[StreamProtocolHTTPrequest::METHOD_COUNT] = {
	"HEAD",
	"GET",
	"POST"
};

StreamProtocolHTTPresponse::StreamProtocolHTTPresponse(Socket::ClientSocket _socket) : StreamProtocol(_socket){
	//
}

StreamProtocolHTTPresponse::~StreamProtocolHTTPresponse(){
	//
}

bool StreamProtocolHTTPresponse::Read(){
	return false;
}

//Send the response (or part of it) to the client. Returns true when everything has been sent.
bool StreamProtocolHTTPresponse::Write(){
	tbb_string spresstr(buffer.begin(),buffer.end());
	ssize_t res = spresstr.size();
	ssize_t len = socket.Send(spresstr.c_str(),res);
	//printf("--------\n%s--------\n",spresstr.c_str()); //debug

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

//Reset the response data to the state of a new client
void StreamProtocolHTTPresponse::Reset(){
	state = STATE_PENDING;
	buffer.clear();
}

//Generate the response content: format it from the given request lines
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

	static const char *pcrlf = "\r\n";
	buffer.insert(buffer.end(),pcrlf,pcrlf+2);
}

void StreamProtocolHTTPresponse::Generate(STATUS status, bool crlf){
	Generate(pstatstr[status],crlf);
}

//Prepare a response header before the response is generated
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

//Format and prepare a time string header (Last-Modified, for example)
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

//Send (possibly automatically generated) data buffer (or part of it).
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

//Send a file (or part of it)
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
	//fopen failing may not be a sufficient check in case of directories. However, the type of the file is checked before Open().
	//It's unlikely to change between these two operations.
	if(!(pf = fopen(path,"rb")))
		return false;
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

//Read data from a CGI pipe and send it immediately afterwards. As with the rest of Write() methods, returns true
//when the pipe is closed and no more data is to be read and sent.
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
		if(dt < 5e9) //TODO: user defined CGI timeout
			return false;
		state = STATE_SUCCESS;
		return true;
	}

	errno = 0;

	//feedback == false -> no double-CRLF encountered, yet
	if(!feedback){
		//Some of the output has to be checked in case of any status headers. CGI scripter may manually assign the HTTP response status.
		//Until double-CRLF is found, data has to be buffered.
		buffer.insert(buffer.end(),buffer1,buffer1+res);

		size_t postl;
		if(ClientProtocolHTTP::FindBreak(&buffer,&postl)){
			tbb_string feedbstr(buffer.begin(),buffer.begin()+postl), status;
			if(ClientProtocolHTTP::ParseHeader(0,"\r\n"+feedbstr,"Status",status))
				pres->Generate(status.c_str(),false); //if 'Status' was found, generate the response with it
			else pres->Generate(Protocol::StreamProtocolHTTPresponse::STATUS_200,false); //otherwise return 200. This has to be fixed for user error pages

			//TODO: Check if Content-Length is given. If the amount of data read is small, it can also be determined here so that
			//persistent connection can be used.
			buffer.insert(buffer.begin(),pres->buffer.begin(),pres->buffer.end());
			feedback = true;

			tbb_string spresstr(buffer.begin(),buffer.end());
		}
	}

	if(feedback){
		//Before reading any new content from the pipe, empty the buffer. The buffer fills up when initially searching the status feedback
		//or when send() failed to deliver everything.
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

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts); //update the CGI timeout counter

	return false;
}

//Receive POST data (the remaining that did not come with the request packet) and write it to CGI pipe. Returns true when everything has been
//received that written.
bool StreamProtocolCgi::Read(){
	char buffer1[4096];
	ssize_t len = socket.Recv(buffer1,sizeof(buffer1));

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		close(pipefdo[1]);
		state = STATE_CLOSED;
		return true;
	}

	datac += len;

	size_t leak = 0;
	if(datac > datal){
		//Any excess data is buffered for the POST pipelining - when CGI Content-Length is supported. Currently this is useless.
		leak = datac-datal;
		preq->buffer.insert(preq->buffer.end(),buffer1+len-leak,buffer1+len);
	}

	//Assume that the pipe is available for writing. We don't check the number of bytes written.
	write(pipefdo[1],buffer1,len-leak);

	if(datac >= datal){
		close(pipefdo[1]);
		state = STATE_SUCCESS;
		return true;
	}

	//update the CGI read timeout counter, in case the next write round will be the last.
	clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts);

	return false;
}

//Clear the buffers and close the pipes (in case they're still hanging)
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

//Spawn the CGI child process and write any possible (partial) POST data that came with the request
bool StreamProtocolCgi::Open(const char *pcgibin, const char *pcgiarg, size_t _datal, StreamProtocolHTTPrequest *_preq, StreamProtocolHTTPresponse *_pres){
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

		//execle("/usr/bin/php-cgi","php-cgi",0,envptr.data());
		execle(pcgibin,pcgiarg,0,envptr.data());
		//
		exit(0); //exit child in case of failure
	}

	close(pipefdo[0]);
	close(pipefdi[1]);

	fcntl(pipefdi[0],F_SETFL,fcntl(pipefdi[0],F_GETFL,0)|O_NONBLOCK); //non-blocking pipe

	clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&ts);
	preq = _preq;
	pres = _pres;
	datac = 0;
	datal = _datal;

	if(preq->postl < preq->buffer.size()){
		//Some of the POST data has already been received. Get it from the request instance and write it here.
		tbb_string leakstr(preq->buffer.begin()+preq->postl,preq->buffer.end());
		datac = leakstr.size();

		size_t leak = 0;
		if(datac > datal){
			//All of POST was in the request buffer. This may also contain something from the
			//next request - again some pipelining considerations.
			leak = datac-datal;
		}

		write(pipefdo[1],leakstr.c_str(),datac-leak); //TODO: leakwrite
		if(datac >= datal)
			close(pipefdo[1]);

		preq->postl = datal; //on Reset(), the request data will be erased up to the end of the POST
	}

	return true;
}

ClientProtocolHTTP::ClientProtocolHTTP(Socket::ClientSocket _socket, ServerInterface *_psi, ClientInterface *_pci, boost::python::object _clobj) : ClientProtocol(_socket,PROTOCOL_RECV), psi(_psi), pci(_pci), clobj(_clobj), spreq(_socket), spres(_socket), spdata(_socket), spfile(_socket), spcgi(_socket){
	Reset();
}

ClientProtocolHTTP::~ClientProtocolHTTP(){
	//
	socket.Close();
}

ClientProtocol::POLL ClientProtocolHTTP::Poll(uint sflag1){
	//Main HTTP state machine. Check the state and according to the HTTP protocol, decide what to do next. Here a specific
	//state means that the operation corresponding to it has just been completed.

	if(sflag1 == PROTOCOL_ACCEPT)
		return POLL_SKIP; //State machine for this client already initialized

	//No need to check sflags (the epoll flags driver), since only either PROTOCOL_SEND or RECV is enabled according to current state
	if(state == STATE_RECV_REQUEST){
		if(spreq.state == StreamProtocol::STATE_CLOSED)
			return POLL_CLOSE;

		sflags = 0; //Do not expect any traffic until the request has been processed
		return POLL_RUN; //Handle the request in the parallelizable Run()

	}else
	if(state == STATE_RECV_DATA){
		//A long POST just finished arriving. Prepare to read the CGI pipe.
		if(spcgi.state == StreamProtocol::STATE_CLOSED)
			return POLL_CLOSE;

		psp = &spcgi;
		state = STATE_SEND_DATA;
		sflags = PROTOCOL_SEND;

	}else
	if(state == STATE_SEND_RESPONSE){
		if(content == CONTENT_NONE){
			Clear();
			if(connection == CONNECTION_KEEPALIVE){
				Reset();
				//Check pipelined requests and move to process them immediately if present
				if(spreq.CheckPipeline()){
					sflags = 0;
					return POLL_RUN;
				}
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
		//sflags = PROTOCOL_SEND; //keep sending (already set)

		return POLL_SKIP;
	}else
	if(state == STATE_SEND_DATA){
		//Data, file or whatever just finished uploading
		{
			Clear();
			if(connection == CONNECTION_KEEPALIVE){
				Reset();
				if(spreq.CheckPipeline()){
					sflags = 0;
					return POLL_RUN;
				}
				return POLL_SKIP;
			}else return POLL_CLOSE;
		}
	}

	//default queries
	return POLL_SKIP;
}

void ClientProtocolHTTP::Reset(){
	//Reset the client to the state of post-accept
	psp = &spreq;
	state = STATE_RECV_REQUEST;
	sflags = PROTOCOL_RECV;

	method = StreamProtocolHTTPrequest::METHOD_GET;
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

//After the request has completely arrived, we move to process it. This is the thread-safe
//request parsing part right before the Python script configuration.
void ClientProtocolHTTP::Accept(){
	try{
		if(spreq.state == StreamProtocol::STATE_CORRUPTED)
			throw(StreamProtocolHTTPresponse::STATUS_413); //or 400

		spreqstr = tbb_string(spreq.buffer.begin(),spreq.buffer.begin()+spreq.postl);

		//https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html (HTTP/1.1 request)
		//https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.5
		lf = spreqstr.find("\r\n"); //Find the first CRLF. No newlines allowed in Request-Line
		tbb_string request = spreqstr.substr(0,lf);

		//parse the request line ----------------------------------------------------------------------------

		//https://www.w3.org/Protocols/rfc2616/rfc2616-sec9.html (methods)
		static const uint ml[] = {5,4,5};
		if(request.compare(0,ml[0],"HEAD ") == 0)
			method = StreamProtocolHTTPrequest::METHOD_HEAD;
		else
		if(request.compare(0,ml[1],"GET ") == 0)
			method = StreamProtocolHTTPrequest::METHOD_GET;
		else
		if(request.compare(0,ml[2],"POST ") == 0)
			method = StreamProtocolHTTPrequest::METHOD_POST;
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

		requri_enc = request.substr(ru,rl-ru);
		enclen = requri_enc.size();

		//https://tools.ietf.org/html/rfc3986#section-3 (syntax components)
		qv = requri_enc.find('?',0); //find the beginning of the query string
		if(qv == std::string::npos)
			qv = enclen;

		//Very basic uri decoding. Needs a better version to support more cases.
		tbb_string requri_dec = "/";
		requri_dec.reserve(qv);
		for(uint i = 1; i < qv; ++i){
			if(requri_enc[i] == '%'){
				if(i >= qv-2)
					break;
				tbb_string enc = requri_enc.substr(i+1,2);
				ulong c = strtoul(enc.c_str(),0,16);
				if(c != 0 && c < 256)
					requri_dec += (char)c;
				i += 2;
			}else requri_dec += requri_enc[i];
		}

		//parse and remove dot (./..) segments
		ParseSegments(requri_dec,pci->resource);

		//parse the relevant headers ------------------------------------------------------------------------

		//initialize or restore the default settings
		pci->ResetConfig();

		if(!ParseHeader(lf,spreqstr,"Host",pci->host))
			throw(StreamProtocolHTTPresponse::STATUS_400); //always required by the 1.1 standard
		ParseHeader(lf,spreqstr,"Referer",pci->referer);
		ParseHeader(lf,spreqstr,"Cookie",pci->cookie);
		ParseHeader(lf,spreqstr,"User-Agent",pci->useragent);

		char address[256] = "0.0.0.0";
		socket.Identify(address,sizeof(address));

		tbb_string hcnt;
		if(ParseHeader(lf,spreqstr,"Connection",hcnt) && hcnt.compare(0,5,"close") == 0)
			connection = CONNECTION_CLOSE;
		else connection = CONNECTION_KEEPALIVE;

		pci->uri = requri_enc;
		pci->address = tbb_string(address);
		ParseHeader(lf,spreqstr,"Accept",pci->accept);
		ParseHeader(lf,spreqstr,"Accept-Encoding",pci->acceptenc);
		ParseHeader(lf,spreqstr,"Accept-Language",pci->acceptlan);

		status = StreamProtocolHTTPresponse::STATUS_200;

	}catch(Protocol::StreamProtocolHTTPresponse::STATUS _status){

		//catch reserved
		status = _status;
	}
}

//Execute the client configuration python script. Not thread-safe.
void ClientProtocolHTTP::Configure(){
	if(status == StreamProtocolHTTPresponse::STATUS_200)
		pci->Setup();
}

//Process the request: open the file or pipe, or generate the content. Thread-safe again.
void ClientProtocolHTTP::Process(){
	try{
		if(status != StreamProtocolHTTPresponse::STATUS_200)
			throw(status); //forward the status from Accept() if not 200

		tbb_string locald = pci->root+pci->resource; //TODO: check the object type

		if(pci->cgi)
			connection = CONNECTION_CLOSE; //Length of the content unknown, for now. Might be fixed later.
		else
		if(method == StreamProtocolHTTPrequest::METHOD_POST){
			spres.AddHeader("Allow","GET,HEAD");
			throw(StreamProtocolHTTPresponse::STATUS_405); //only scripts shall accept POST
		}

		struct stat statbuf;
		if(stat(locald.c_str(),&statbuf) == -1)
			throw(StreamProtocolHTTPresponse::STATUS_404);

		if(pci->deny)
			throw(StreamProtocolHTTPresponse::STATUS_403);

		time_t modsince = ~0;
		tbb_string hcnt;
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

			if(pci->index){
#ifdef USE_CUSTOMDIRINDEX
				tbb_string page = locald+pci->indexfile;
				if(stat(page.c_str(),&statbuf) != -1 && !S_ISDIR(statbuf.st_mode))
					locald = page;
#else
				static const char *pindex[] = {"index.html","index.htm","index.shtml","index.php","index.py","index.pl","index.cgi"};
				for(uint i = 0, n = sizeof(pindex)/sizeof(pindex[0]); i < n; ++i){
					tbb_string page = locald+pindex[i];
					if(stat(page.c_str(),&statbuf) != -1 && !S_ISDIR(statbuf.st_mode)){
						locald = page;
						break;
					}
				}
#endif
			}
		}

		spres.AddHeader("Connection",connection == CONNECTION_KEEPALIVE?"keep-alive":"close");

		if(S_ISDIR(statbuf.st_mode)){
			//index-file not present
			//list the contents of this dir, if enabled
			if(!pci->listing)
				throw(StreamProtocolHTTPresponse::STATUS_404);

			PageGen::HTTPListDir listdir(&spdata);
			if(method != StreamProtocolHTTPrequest::METHOD_HEAD){
				if(!listdir.Generate(locald.c_str(),pci->resource.c_str(),psi))
					throw(StreamProtocolHTTPresponse::STATUS_500);
				content = CONTENT_DATA;
			}

			spres.AddHeader("Content-Type","text/html");
			spres.FormatHeader("Content-Length","%lu",spdata.buffer.size());
			spres.Generate(StreamProtocolHTTPresponse::STATUS_200);

		}else
		if(pci->cgi){
			//https://tools.ietf.org/html/rfc3875
			ulong contentl = 0;
			if(method == StreamProtocolHTTPrequest::METHOD_POST){
				if(!ParseHeader(lf,spreqstr,"Content-Length",hcnt) || !StrToUl(hcnt.c_str(),contentl))
					throw(StreamProtocolHTTPresponse::STATUS_411);
				spcgi.AddEnvironmentVar("CONTENT_LENGTH",hcnt.c_str());
				if(!ParseHeader(lf,spreqstr,"Content-Type",hcnt))
					hcnt.clear();//throw(StreamProtocolHTTPresponse::STATUS_400);
				spcgi.AddEnvironmentVar("CONTENT_TYPE",hcnt.c_str());
			}

			//HTTP_COOKIE
			spcgi.AddEnvironmentVar("HTTP_HOST",pci->host.c_str());
			spcgi.AddEnvironmentVar("HTTP_REFERER",pci->referer.c_str());
			spcgi.AddEnvironmentVar("HTTP_USER_AGENT",pci->useragent.c_str());
			spcgi.AddEnvironmentVar("HTTP_COOKIE",pci->cookie.c_str());
			spcgi.AddEnvironmentVar("HTTP_ACCEPT",pci->accept.c_str());
			spcgi.AddEnvironmentVar("HTTP_ACCEPT_ENCODING",pci->acceptenc.c_str());
			spcgi.AddEnvironmentVar("HTTP_ACCEPT_LANGUAGE",pci->acceptlan.c_str());
			spcgi.AddEnvironmentVar("HTTP_CONNECTION","close");

			static const char *pmstr[] = {"HEAD","GET","POST"};
			spcgi.AddEnvironmentVar("REQUEST_METHOD",pmstr[method]);
			spcgi.AddEnvironmentVar("REQUEST_URI",requri_enc.c_str());

			spcgi.AddEnvironmentVar("GATEWAY_INTERFACE","CGI/1.1");
			spcgi.AddEnvironmentVar("REMOTE_ADDR",pci->address.c_str());
			spcgi.AddEnvironmentVar("REMOTE_HOST",pci->address.c_str());
			//spcgi.AddEnvironmentVar("REMOTE_PORT","8080"); //pci->port
			spcgi.AddEnvironmentVar("QUERY_STRING",qv != enclen?requri_enc.substr(qv+1).c_str():"");
			spcgi.AddEnvironmentVar("REDIRECT_STATUS","200");

			spcgi.AddEnvironmentVar("SERVER_NAME",psi->name.c_str());
			//spcgi.AddEnvironmentVar("SERVER_ADDR","localhost");
			//spcgi.AddEnvironmentVar("SERVER_PORT","8080");
			spcgi.AddEnvironmentVar("SERVER_PROTOCOL","HTTP/1.1");
			spcgi.AddEnvironmentVar("SERVER_SOFTWARE","tighttpd/0.1");

			spcgi.AddEnvironmentVar("SCRIPT_NAME",pci->resource.c_str());
			spcgi.AddEnvironmentVar("SCRIPT_FILENAME",locald.c_str());
			spcgi.AddEnvironmentVar("DOCUMENT_ROOT",pci->root.c_str());
			{
				if(!spcgi.Open(pci->cgibin.c_str(),pci->cgiarg.c_str(),contentl,&spreq,&spres))
					throw(StreamProtocolHTTPresponse::STATUS_500);
				content = CONTENT_CGI;
			}

			//Delayed response generation: cgi class handles this after checking the status feedback
			//spres.Generate(StreamProtocolHTTPresponse::STATUS_200,false);

		}else
		if(modsince != statbuf.st_mtime){
			if(method != StreamProtocolHTTPrequest::METHOD_HEAD){
				if(!spfile.Open(locald.c_str()))
					throw(StreamProtocolHTTPresponse::STATUS_500); //send 500 since the file was supposed to exist
				content = CONTENT_FILE;
			}

			spres.AddHeader("Content-Type",pci->mimetype.c_str());
			spres.FormatHeader("Content-Length","%lu",statbuf.st_size);
			spres.FormatTime("Last-Modified",&statbuf.st_mtime);
			spres.Generate(StreamProtocolHTTPresponse::STATUS_200);
		}else{
			//content = CONTENT_NONE;
			spres.Generate(StreamProtocolHTTPresponse::STATUS_304);
		}

		if(method == StreamProtocolHTTPrequest::METHOD_POST && spcgi.datac < spcgi.datal){
			//Receive rest of the POST if request packet didn't leak it already
			psp = &spcgi;
			state = STATE_RECV_DATA;
			sflags = PROTOCOL_RECV; //re-enable EPOLLIN
		}else
		if(pci->cgi){
			//In case of cgi, the response is integrated to the content due to need to check the status feedback
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

			if(method != StreamProtocolHTTPrequest::METHOD_HEAD)
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
}

void ClientProtocolHTTP::ParseSegments(tbb_string &uri, tbb_string &out){
	//dot segment parsing according to https://tools.ietf.org/html/rfc3986#section-5.2.4
	out.clear();
	for(;;){
		if(uri.compare(0,2,"./") == 0){
			uri.erase(uri.begin(),uri.begin()+2);
			continue;
		}else
		if(uri.compare(0,3,"../") == 0){
			uri.erase(uri.begin(),uri.begin()+3);
			continue;
		}else
		if(uri.compare(0,3,"/./") == 0){
			uri.replace(uri.begin(),uri.begin()+3,"/");
			continue;
		}else
		if(uri.compare(0,std::string::npos,"/.") == 0){
			uri.replace(uri.begin(),uri.begin()+2,"/");
			continue;
		}else
		if(uri.compare(0,4,"/../") == 0){
			uri.replace(uri.begin(),uri.begin()+4,"/");
			size_t t = out.rfind('/');
			if(t == std::string::npos)
				t = 0;
			out.erase(out.begin()+t,out.end());
		}else
		if(uri.compare(0,std::string::npos,"/..") == 0){
			uri.replace(uri.begin(),uri.begin()+3,"/");
			size_t t = out.rfind('/');
			if(t == std::string::npos)
				t = 0;
			out.erase(out.begin()+t,out.end());
		}else
		if(uri.compare(0,std::string::npos,".") == 0){
			uri.erase(uri.begin(),uri.begin()+1);
			continue;
		}else
		if(uri.compare(0,std::string::npos,"..") == 0){
			uri.erase(uri.begin(),uri.begin()+2);
			continue;
		}else{
			size_t t = uri.find('/',1);
			out += uri.substr(0,t);
			if(t == std::string::npos)
				break;
			else uri.erase(uri.begin(),uri.begin()+t);
		}
	}
}

//Find and return the contents of a request header line. A format "\r\nHeader: Data" is assumed.
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

//Convert string to unsigned long with some additional checks.
bool ClientProtocolHTTP::StrToUl(const char *pstr, ulong &ul){
	char *pendptr = 0;
	errno = 0;

	ul = strtoul(pstr,&pendptr,10);
	return !(errno == ERANGE || *pendptr != 0 || pstr == pendptr);
}

//Find double-CRLF from the text buffer
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
