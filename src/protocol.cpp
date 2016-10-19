#include "main.h"
#include "socket.h"
#include "protocol.h"

#include <stdarg.h> //FormatHeader()
#include <time.h> //date header field
#include <sys/stat.h> //file stats

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

void StreamProtocolHTTPresponse::Generate(STATUS status){
	static const char *pstatstr[] = {
		"200 OK",
		"303 See Other",
		"304 Not Modified",
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

	time_t rt;
	time(&rt);
	const struct tm *pti = gmtime(&rt);
#define DATEFMT_RFC1123 "%a, %d %b %Y %H:%M:%S %Z"
	len = strftime(buffer1,sizeof(buffer1),"Date: " DATEFMT_RFC1123 "\r\n",pti); //mandatory
	buffer.insert(buffer.begin(),buffer1,buffer1+len);

	len = snprintf(buffer1,sizeof(buffer1),"HTTP/1.1 %s\r\nServer: tighttpd/0.1\r\n",pstatstr[status]);
	buffer.insert(buffer.begin(),buffer1,buffer1+len);

	static const char *pclrf = "\r\n";
	buffer.insert(buffer.end(),pclrf,pclrf+2);
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
	Reset();
}

ClientProtocolHTTP::~ClientProtocolHTTP(){
	//
	socket.Close();
}

ClientProtocol::POLL ClientProtocolHTTP::Poll(uint sflag1){
	//main HTTP state machine
	if(sflag1 == PROTOCOL_ACCEPT)
		return POLL_SKIP;

	//no need to check sflags, since only either PROTOCOL_SEND or RECV is enabled according to current state
	if(state == STATE_RECV_REQUEST || state == STATE_RECV_DATA){
		//sflag1 == PROTOCOL_RECV
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

		const char test[] = "Some content\r\n";
		spdata.Append(test,strlen(test));

		psp = (content == CONTENT_DATA)?
			(StreamProtocolData*)&spdata:(StreamProtocolData*)&spfile;
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

	//default queries (ACCEPT)
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

			tbb_string spreqstr(spreq.buffer.begin(),spreq.buffer.end());

			//https://www.w3.org/Protocols/rfc2616/rfc2616-sec5.html (HTTP/1.1 request)
			//https://www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.5
			size_t lf = spreqstr.find("\r\n"); //Find the first CRLF. No newlines allowed in Request-Line
			tbb_string request = spreqstr.substr(0,lf);

			//parse the request line ----------------------------------------------------------------------------
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

			//std::istringstream iss(resource);
			/*tbb_istringstream iss(requri_enc);
			for(tbb_string tok; getline(iss,tok,'%');){
				//resource += tok;
				printf("--%s\n",tok.c_str());
			}*/

			//basic uri decoding
			tbb_string resource = "/";
			resource.reserve(qv);
			for(uint i = 1; i < qv; ++i){
				if(requri_enc[i] == '%'){
					if(i >= qv-2)
						break;
					tbb_string enc = requri_enc.substr(i+1,2);
					ulong c = strtoul(enc.c_str(),0,16);
					if(c != 0 && c < 256)
						resource += (char)c;
					i += 2;
				}else resource += requri_enc[i];
			}

			//TODO: decode the query (after &-tokenizing)?

			//parse the relevant headers ------------------------------------------------------------------------
			tbb_string hcnt;
			if(!ParseHeader(lf,spreqstr,"Host",hcnt))
				throw(StreamProtocolHTTPresponse::STATUS_400); //always required by 1.1 standard

			char address[256] = "0.0.0.0";
			socket.Identify(address,sizeof(address));

			if(ParseHeader(lf,spreqstr,"Connection",hcnt) && hcnt.compare(0,10,"keep-alive") == 0)
				connection = CONNECTION_KEEPALIVE;
			else connection = CONNECTION_CLOSE;

			PyObject_SetAttrString(psub,"uri",PyUnicode_FromString(requri_enc.c_str()));
			PyObject_SetAttrString(psub,"resource",PyUnicode_FromString(resource.c_str()));
			PyObject_SetAttrString(psub,"host",PyUnicode_FromString(hcnt.c_str()));
			PyObject_SetAttrString(psub,"address",PyUnicode_FromString(address));
			PyObject_SetAttrString(psub,"connection",PyLong_FromLong(connection));

			if(ParseHeader(lf,spreqstr,"User-Agent",hcnt))
				PyObject_SetAttrString(psub,"useragent",PyUnicode_FromString(hcnt.c_str()));
			if(ParseHeader(lf,spreqstr,"Accept",hcnt))
				PyObject_SetAttrString(psub,"accept",PyUnicode_FromString(hcnt.c_str()));
			if(ParseHeader(lf,spreqstr,"Accept-Encoding",hcnt))
				PyObject_SetAttrString(psub,"accept_encoding",PyUnicode_FromString(hcnt.c_str()));
			if(ParseHeader(lf,spreqstr,"Accept-Language",hcnt))
				PyObject_SetAttrString(psub,"accept_language",PyUnicode_FromString(hcnt.c_str()));

			//prepare the default options
			PyObject_SetAttrString(psub,"root",PyUnicode_FromString(".")); //current work dir
			PyObject_SetAttrString(psub,"index",PyBool_FromLong(true)); //enable index pages
			PyObject_SetAttrString(psub,"listing",PyBool_FromLong(false)); //disable directory listing by default
			PyObject_SetAttrString(psub,"mimetype",PyUnicode_FromString("application/octet-stream")); //let the config determine this
			//PyObject_SetAttrString(psub,"index",PyUnicode_FromString("index.html"));

			if(!PyEval_EvalCode(pycode,pyglb,0)){
				PyErr_Print();

				throw(StreamProtocolHTTPresponse::STATUS_500);
			}

			//retrieve the configured options
			PyObject *pycfg;

			pycfg = PyObject_GetAttrString(psub,"root");
			tbb_string locald = tbb_string(PyUnicode_AsUTF8(pycfg))+resource; //TODO: check the object type
			Py_DECREF(pycfg);

			pycfg = PyObject_GetAttrString(psub,"index");
			bool index = PyObject_IsTrue(pycfg);
			Py_DECREF(pycfg);

			pycfg = PyObject_GetAttrString(psub,"listing");
			bool listing = PyObject_IsTrue(pycfg);
			Py_DECREF(pycfg);

			pycfg = PyObject_GetAttrString(psub,"mimetype");
			tbb_string mimetype = tbb_string(PyUnicode_AsUTF8(pycfg));
			Py_DECREF(pycfg);

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

			if(S_ISDIR(statbuf.st_mode)){
				//Forward directory requests with /[uri] to /[uri]/
				if(requri_enc.back() != '/'){
					requri_enc += '/';
					spres.FormatHeader("Location",requri_enc.c_str());
					throw(StreamProtocolHTTPresponse::STATUS_303);
				}

				if(index){
					static const char *pindex[] = {"index.html","index.htm","index.shtml","index.php"};
					for(uint i = 0, n = sizeof(pindex)/sizeof(pindex[0]); i < n; ++i){
						tbb_string page = locald+pindex[i];
						if(stat(page.c_str(),&statbuf) != -1 && !S_ISDIR(statbuf.st_mode)){
							locald = page;
							break;
						}
					}
				}

				if(S_ISDIR(statbuf.st_mode)){
					//list the contents of this dir, if enabled
					if(listing){
						//
					}else{
						throw(StreamProtocolHTTPresponse::STATUS_404);
					}
				}
			}

			/*if(method != METHOD_HEAD && modsince != statbuf.st_mtime){
				if(!spfile.Open(path))
					throw(StreamProtocolHTTPresponse::STATUS_500); //send 500 since file was supposed to exist

				content = CONTENT_FILE;
			}*/

			spres.AddHeader("Connection",connection == CONNECTION_KEEPALIVE?"keep-alive":"close");
			//
			/*spres.AddHeader("Content-Type",mimetype.c_str());
			spres.FormatHeader("Content-Length","%u",statbuf.st_size);
			spres.FormatTime("Last-Modified",&statbuf.st_mtime);*/

			if(modsince != statbuf.st_mtime){
				if(method != METHOD_HEAD){
					if(!spfile.Open(path))
						throw(StreamProtocolHTTPresponse::STATUS_500); //send 500 since file was supposed to exist
					content = CONTENT_FILE;
				}

				spres.AddHeader("Content-Type",mimetype.c_str());
				spres.FormatHeader("Content-Length","%u",statbuf.st_size);
				spres.FormatTime("Last-Modified",&statbuf.st_mtime);
				spres.Generate(StreamProtocolHTTPresponse::STATUS_200); //don't finalize if the preprocessor wants to add something
			}else{
				//content = CONTENT_NONE;
				spres.Generate(StreamProtocolHTTPresponse::STATUS_304);
			}

			/*if(modsince == statbuf.st_time){
				content = CONTENT_NONE;
				spres.Generate(StreamProtocolHTTPresponse::STATUS_304);
			}else spres.Generate(StreamProtocolHTTPresponse::STATUS_200);*/

			//In case of preprocessor, prepare another StreamProtocol.
			//Determine if the preprocessor wants to override any of the response headers, like the Content-Type.

			//Get the file size or prepare StreamProtocolData and determine its final length.
			//Connection: keep-alive requires Content-Length

			if(method == METHOD_POST){
				psp = &spdata;
				state = STATE_RECV_DATA;
				sflags = PROTOCOL_RECV; //re-enable EPOLLIN
			}else{
				psp = &spres;
				state = STATE_SEND_RESPONSE;
				sflags = PROTOCOL_SEND; //switch to EPOLLOUT
			}

		}catch(Protocol::StreamProtocolHTTPresponse::STATUS status){

			spres.AddHeader("Connection",connection == CONNECTION_KEEPALIVE?"keep-alive":"close");
			spres.AddHeader("Content-Length","0"); //until we read or generate the error pages
			spres.Generate(status);

			//prepare the fail response
			{
				psp = &spres;
				state = STATE_SEND_RESPONSE;
				sflags = PROTOCOL_SEND;
			}
		}
	}else
	if(state == STATE_RECV_DATA){
		if(spdata.state == StreamProtocol::STATE_CLOSED)
			return false;
		//POST complete
		//write it to preprocessor stdin or whatever

		//state = STATE_SEND_RESPONSE;
		//sflags = PROTOCOL_SEND;
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

bool ClientProtocolHTTP::InitConfigModule(PyObject *pmod, const char *pcfgsrc){
	static const char *psubn = "http";
	//static struct PyModuleDef ghttpmod = {PyModuleDef_HEAD_INIT,psubn,"doc",-1,ghttpmeth,0,0,0,0};
	static struct PyModuleDef ghttpmod = {PyModuleDef_HEAD_INIT,psubn,"doc",-1,0,0,0,0,0};

	psub = PyModule_Create(&ghttpmod);
	PyModule_AddObject(pmod,psubn,psub);

	PyModule_AddIntConstant(psub,"port",8080);

	PyModule_AddIntConstant(psub,"CONNECTION_CLOSE",CONNECTION_CLOSE);
	PyModule_AddIntConstant(psub,"CONNECTION_KEEPALIVE",CONNECTION_KEEPALIVE);
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
