#include "main.h"
#include "socket.h"
#include "protocol.h"

#include <stdarg.h> //FormatHeader()
#include <time.h> //date header field
#include <sys/stat.h> //file stats

#include <tbb/scalable_allocator.h>
#include <tbb/cache_aligned_allocator.h>

namespace Protocol{

typedef std::basic_string<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>> tbb_string;

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
	size_t len = socket.Recv(buffer1,sizeof(buffer1));

	if((len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) || len == 0){
		state = STATE_CLOSED; //Some error occurred or socket was closed
		return true;
	}

	size_t req = strlen(buffer1);
	if(req < len || buffer.size()+len > 50000){
		state = STATE_CORRUPTED;
		return true; //HTTP 413
	}

	buffer.insert(buffer.end(),buffer1,buffer1+len);
	//Assume that at least "Host:\r\n" is given, as it should be. This makes two CRLFs.
	if(strstr(buffer1,"\r\n\r\n")){
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
	size_t res = spresstr.size();
	size_t len = socket.Send(spresstr.c_str(),res);
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

void StreamProtocolHTTPresponse::Initialize(STATUS status){
	static const char *pstatstr[] = {
		"200 OK",
		"304 Not Found",
		"400 Bad Request",
		"403 Forbidden",
		"404 Not Found",
		"413 Request Entity Too Large",
		"500 Internal Server Error",
		"501 Not Implemented",
		"505 HTTP Version Not Supported"
	};

	char buffer1[4096];
	size_t len;

	len = snprintf(buffer1,sizeof(buffer1),"HTTP/1.1 %s\r\nServer: tighttpd/0.1\r\n",pstatstr[status]);
	buffer.insert(buffer.end(),buffer1,buffer1+len);

	time_t rt;
	time(&rt);
	const struct tm *pti = gmtime(&rt);
	len = strftime(buffer1,sizeof(buffer1),"Date: %a, %d %b %Y %H:%M:%S %Z\r\n",pti); //mandatory
	buffer.insert(buffer.end(),buffer1,buffer1+len);
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

void StreamProtocolHTTPresponse::Finalize(){
	static const char *pclrf = "\r\n";
	buffer.insert(buffer.end(),pclrf,pclrf+2);
}

StreamProtocolData::StreamProtocolData(Socket::ClientSocket _socket) : StreamProtocol(_socket){
	//
}

StreamProtocolData::~StreamProtocolData(){
	//
}

bool StreamProtocolData::Write(){
	tbb_string spresstr(buffer.begin(),buffer.end());
	size_t res = spresstr.size();
	size_t len = socket.Send(spresstr.c_str(),res);

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

StreamProtocolFile::StreamProtocolFile(Socket::ClientSocket _socket) : StreamProtocol(_socket), pf(0){
	//
}

StreamProtocolFile::~StreamProtocolFile(){
	//
}

bool StreamProtocolFile::Write(){
	char buffer1[4096];
	size_t res = fread(buffer1,1,sizeof(buffer1),pf);
	size_t len = socket.Send(buffer1,res);

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

void StreamProtocolData::Append(const char *pdata, size_t datal){
	buffer.insert(buffer.end(),pdata,pdata+datal);
}

ClientProtocolHTTP::ClientProtocolHTTP(Socket::ClientSocket _socket) : ClientProtocol(_socket,PROTOCOL_RECV), spreq(_socket), spres(_socket), spdata(_socket), spfile(_socket){
	psp = &spreq;
	state = STATE_RECV_REQUEST;

	method = METHOD_GET;
	content = CONTENT_NONE;
}

ClientProtocolHTTP::~ClientProtocolHTTP(){
	//
	socket.Close();
}

ClientProtocol::POLL ClientProtocolHTTP::Poll(uint sflag1){
	//main HTTP state machine
	if(sflag1 == PROTOCOL_ACCEPT){
		//Nothing to do, all the relevant flags have already been set in class constructor. Awaiting for request.
		return POLL_SKIP;
	}

	//no need to check sflags, since only either PROTOCOL_SEND or RECV is enabled according to current state
	if(state == STATE_RECV_REQUEST || state == STATE_RECV_DATA){
		//sflag1 == PROTOCOL_RECV
		sflags = 0; //do not expect traffic until request has been processed
		return POLL_RUN; //handle the request in Run()

	}else
	if(state == STATE_SEND_RESPONSE){
		if(content == CONTENT_NONE)
			return POLL_CLOSE;

		const char test[] = "Some content\r\n";
		spdata.Append(test,strlen(test));

		psp = (content == CONTENT_DATA)?
			(StreamProtocolData*)&spdata:(StreamProtocolData*)&spfile;
		state = STATE_SEND_DATA;
		//sflags = PROTOCOL_SEND; //keep sending

		return POLL_SKIP;
	}else
	if(state == STATE_SEND_DATA){
		psp->Reset();
		return POLL_CLOSE;
	}

	return POLL_SKIP;
}

void ClientProtocolHTTP::Run(){
	//
	if(state == STATE_RECV_REQUEST){
		try{
			if(spreq.state == StreamProtocol::STATE_CORRUPTED){
				spres.Initialize(StreamProtocolHTTPresponse::STATUS_413); //or 400
				spres.Finalize();
				throw(0);
			}else
			if(spreq.state == StreamProtocol::STATE_ERROR){
				spres.Initialize(StreamProtocolHTTPresponse::STATUS_500);
				spres.Finalize();
				throw(0);
			}else
			if(spreq.state == StreamProtocol::STATE_CLOSED){
				//remove the client
				return;
			}

			tbb_string spreqstr(spreq.buffer.begin(),spreq.buffer.end());

			//https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html (HTTP/1.1 request)
			//https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.5
			size_t lf = spreqstr.find("\r\n"); //Find the first CRLF. No newlines allowed in Request-Line
			tbb_string request = spreqstr.substr(0,lf);

			//https://www.w3.org/Protocols/rfc2616/rfc2616-sec9.html (methods)
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
				spres.Initialize(StreamProtocolHTTPresponse::STATUS_501);
				spres.Finalize();
				throw(0);
			}

			size_t ru = request.find_first_not_of(" ",ml[method]);
			if(ru == std::string::npos || request[ru] != '/'){
				spres.Initialize(StreamProtocolHTTPresponse::STATUS_400);
				spres.Finalize();
				throw(0);
			}
			size_t rl = request.find(' ',ru+1);
			if(rl == std::string::npos){
				spres.Initialize(StreamProtocolHTTPresponse::STATUS_400);
				spres.Finalize();
				throw(0);
			}
			size_t hv = request.find_first_not_of(" ",rl+1);
			if(hv == std::string::npos || request.compare(hv,8,"HTTP/1.1") != 0){
				spres.Initialize(StreamProtocolHTTPresponse::STATUS_505);
				spres.Finalize();
				throw(0);
			}
			tbb_string requri = request.substr(ru,rl-ru);

			//parse the relevant headers
			size_t hs = spreqstr.find("\r\nHost: ",lf);
			if(hs == std::string::npos){
				spres.Initialize(StreamProtocolHTTPresponse::STATUS_400); //always required by 1.1 standard
				spres.Finalize();
				throw(0);
			}
			size_t he = spreqstr.find("\r\n",hs+8);
			size_t hc = spreqstr.find_first_not_of(" ",hs+8);
			if(he <= hc){
				spres.Initialize(StreamProtocolHTTPresponse::STATUS_400);
				spres.Finalize();
				throw(0);
			}
			tbb_string host = spreqstr.substr(hc,he-hc);

			//config
			char address[256] = "0.0.0.0";
			socket.Identify(address,sizeof(address));

			PyObject_SetAttrString(psub,"uri",PyUnicode_FromString(requri.c_str()));
			PyObject_SetAttrString(psub,"host",PyUnicode_FromString(host.c_str()));
			PyObject_SetAttrString(psub,"address",PyUnicode_FromString(address));
			if(!PyEval_EvalCode(pycode,pyglb,0)){
				PyErr_Print();

				spres.Initialize(StreamProtocolHTTPresponse::STATUS_500);
				spres.Finalize();
				throw(0);
			}

			tbb_string locald = tbb_string(".")+requri;

			const char *path = locald.c_str(); //TODO: security checks (for example handle '..' etc)
			//printf("%s|\n",path); //debug

			struct stat statbuf;
			if(stat(path,&statbuf) == -1){
				spres.Initialize(StreamProtocolHTTPresponse::STATUS_404);
				spres.Finalize();
				//TODO: set psp to 404 file.
				throw(0);
			}

			/*int statr, d = 0;
			do{
				tbb_string t = locald+index[d];
				statr = stat(t.c_str(),&statbuf);
			}while(statr == -1 || S_ISDIR(statbuf.st_mode));*/


			/*if(S_ISDIR(statbuf.st_mode)){
				//TODO: locate the index-file if uri points to a directory
				//alternative list directory if enabled
				//currently this just gives http 500
			}*/

			if(method != METHOD_HEAD){
				//if(!spfile.Open(path)){
				if(S_ISDIR(statbuf.st_mode) || !spfile.Open(path)){
					spres.Initialize(StreamProtocolHTTPresponse::STATUS_500); //send 500 since file was verified to exist
					spres.Finalize();
					throw(0);
				}

				//spres.FormatHeader("Content-Length","%u",statbuf.st_size);
				//Last-Modified

				content = CONTENT_FILE;
			}

			spres.Initialize(StreamProtocolHTTPresponse::STATUS_200); //or 301
			spres.AddHeader("Connection","close");

			//TODO: Python config determines the mimetype? text/html in case of directory

			//in case of preprocessor, prepare another StreamProtocol

			//if keep-alive: spreq.Reset();

			//Get the file size or prepare StreamProtocolData and determine its final length.
			//Connection: keep-alive requires Content-Length

			spres.Finalize(); //don't finalize if the preprocessor wants to add something

			if(method == METHOD_POST){
				psp = &spdata;
				state = STATE_RECV_DATA;
				//
				sflags = PROTOCOL_RECV; //re-enable EPOLLIN
			}else{
				psp = &spres;
				state = STATE_SEND_RESPONSE;
				//
				sflags = PROTOCOL_SEND; //switch to EPOLLOUT
			}

		}catch(...){
			//prepare the fail response
			{
				psp = &spres;
				state = STATE_SEND_RESPONSE;
				sflags = PROTOCOL_SEND;
			}
		}
	}/*else
	if(state == STATE_RECV_DATA){
		//POST complete
		//write it to preprocessor stdin or whatever

		//state = STATE_SEND_RESPONSE;
		//sflags = PROTOCOL_SEND;
	}*/
}

bool ClientProtocolHTTP::InitConfigModule(PyObject *pmod, const char *pcfgsrc){
	static const char *psubn = "http";
	//static struct PyModuleDef ghttpmod = {PyModuleDef_HEAD_INIT,psubn,"doc",-1,ghttpmeth,0,0,0,0};
	static struct PyModuleDef ghttpmod = {PyModuleDef_HEAD_INIT,psubn,"doc",-1,0,0,0,0,0};

	psub = PyModule_Create(&ghttpmod);
	PyModule_AddObject(pmod,psubn,psub);

	PyModule_AddIntConstant(psub,"port",8080);
	//PyModule_AddStringConstant

	pyglb = PyDict_New();
	PyDict_SetItemString(pyglb,"__builtins__",PyEval_GetBuiltins());

	pycode = Py_CompileString(pcfgsrc,"config.py",Py_file_input);
	if(!pycode){
		PyErr_Print();
		return false;
	}

	return true;
}

PyObject * ClientProtocolHTTP::psub = 0;
PyObject * ClientProtocolHTTP::pycode = 0;
PyObject * ClientProtocolHTTP::pyglb = 0;

}
