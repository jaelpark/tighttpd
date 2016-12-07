#include "main.h"
#include "socket.h"
#include "protocol.h"

#include <csignal>
#include <stdlib.h>

#include <time.h> //logging gimmick
#include <stdarg.h>

#include <sys/epoll.h>

#include <boost/python.hpp>

//#include <tbb/flow_graph.h>

ServerInterface::ServerInterface() : name("tighttpd"), port(8080), tls(false){
	//
}

ServerInterface::~ServerInterface(){
	//
}

void ServerInterface::Setup(){
	//
}

void ServerInterface::Accept(){
	//
}

void ServerInterface::ResetConfig(){
	root = ".";
	mimetype = "application/octet-stream";
	index = false;
	listing = true;
	cgi = false;
}

class PythonServerProxy : public ServerInterface, public boost::python::wrapper<ServerInterface>{
public:
	void Setup(){
		boost::python::override ovr = this->get_override("setup");
		if(ovr)
			ovr();
		else ServerInterface::Setup();
	}

	void Accept(){
		boost::python::override ovr = this->get_override("accept");
		if(ovr)
			ovr();
		else ServerInterface::Accept();
	}
};

class PythonServerManager{
public:
	typedef std::pair<Socket::ServerSocket, boost::python::object> SocketObjectPair;
	static void Create(boost::python::object obj){
		ServerInterface &si = boost::python::extract<ServerInterface&>(obj)();
		si.Setup();

		char port[64];
		snprintf(port,sizeof(port),"%u",si.port);

		Socket::ServerSocket server(0);
		int sfd = server.Listen(port);
		if(sfd < 0)
			return;
		DebugPrintf("Listening on port %u...\n",si.port);

		slist.push_back(SocketObjectPair(server,obj));
	}

	static std::vector<SocketObjectPair> slist;
};
std::vector<PythonServerManager::SocketObjectPair> PythonServerManager::slist;

BOOST_PYTHON_MODULE(ServerInterface){
	boost::python::class_<PythonServerProxy,boost::noncopyable>("http")
		.def("setup",&ServerInterface::Setup)
		.def("accept",&ServerInterface::Accept)
		//server config
		//.def_readwrite("software",&ServerInterface::software)
		.def_readwrite("name",&ServerInterface::name)
		.def_readwrite("port",&ServerInterface::port)
		.def_readwrite("tls",&ServerInterface::tls)
		//client config
		.def_readwrite("root",&ServerInterface::root)
		.def_readwrite("mimetype",&ServerInterface::mimetype)
		.def_readwrite("index",&ServerInterface::index)
		.def_readwrite("listing",&ServerInterface::listing)
		.def_readwrite("cgi",&ServerInterface::cgi)
		//client constants
		.def_readonly("host",&ServerInterface::host)
		.def_readonly("uri",&ServerInterface::uri)
		.def_readwrite("resource",&ServerInterface::resource)
		.def_readonly("referer",&ServerInterface::referer)
		.def_readonly("address",&ServerInterface::address)
		.def_readonly("useragent",&ServerInterface::useragent)
		.def_readonly("accept",&ServerInterface::accept)
		.def_readonly("accept_encoding",&ServerInterface::acceptenc)
		.def_readonly("accept_language",&ServerInterface::acceptlan)
		;
	boost::python::def("create",PythonServerManager::Create);
}

int main(int argc, const char **pargv){
	if(argc < 2){
		DebugPrintf("Usage: tighttpd <config.py>\n");
		return -1;
	}

	wchar_t warg[1024];
	mbtowc(warg,pargv[0],sizeof(warg)/sizeof(warg[0]));
	Py_SetProgramName(warg);

	PyImport_AppendInittab("tighttpd",PyInit_ServerInterface);
	Py_Initialize();

	FILE *pf = fopen(pargv[1],"rb");
	if(!pf)
		return 0;
	PyRun_SimpleFile(pf,"config.py");
	fclose(pf);

	signal(SIGINT,[](int s)->void{
		DebugPrintf("Received SIGINT\n");
		for(uint i = 0, n = PythonServerManager::slist.size(); i < n; ++i)
			PythonServerManager::slist[i].first.Close();

		Py_Finalize();
		exit(0);
	});

	//Create the epoll socket monitoring instance
#define MAX_EVENTS 1024
	struct epoll_event event1, events[MAX_EVENTS];
	int efd = epoll_create1(0);
	if(efd == -1){
		DebugPrintf("Error: epoll_create\n");
		Py_Finalize();
		return -1;
	}

	if(PythonServerManager::slist.empty()){
		DebugPrintf("No servers created. Exiting...\n");
		Py_Finalize();
		return -1;
	}

	for(uint i = 0, n = PythonServerManager::slist.size(); i < n; ++i){
		event1.data.ptr = &PythonServerManager::slist[i];
		event1.events = EPOLLIN;
		epoll_ctl(efd,EPOLL_CTL_ADD,PythonServerManager::slist[i].first.fd,&event1);
	}

	event1.data.ptr = &PythonServerManager::slist[0].first;
	event1.events = EPOLLIN;
	epoll_ctl(efd,EPOLL_CTL_ADD,PythonServerManager::slist[0].first.fd,&event1);

	std::queue<Protocol::ClientProtocol *> taskq; //task queue for intensive work
	for(;;){
		//Run() parallel queue
		//serial node for python execution?
		for(int n = epoll_wait(efd,events,MAX_EVENTS,-1), i = 0; i < n; ++i){
			PythonServerManager::SocketObjectPair *psop = 0;
			for(uint j = 0, m = PythonServerManager::slist.size(); j < m; ++j){
				if(events[i].data.ptr == &PythonServerManager::slist[j]){
					psop = &PythonServerManager::slist[j];
					break;
				}
			}
			if(psop){
				for(;;){
					Socket::ClientSocket client(psop->first.Accept());
					if(client.fd == -1)
						break;

					ServerInterface &psi = boost::python::extract<ServerInterface&>(psop->second)();
					Protocol::ClientProtocolHTTP *ptp =
						new Protocol::ClientProtocolHTTP(client,&psi); //,serverinterface

					switch(ptp->Poll(PROTOCOL_ACCEPT)){
					case Protocol::ClientProtocol::POLL_RUN:
						taskq.push(ptp);
						break;
					case Protocol::ClientProtocol::POLL_CLOSE:
						epoll_ctl(efd,EPOLL_CTL_DEL,client.fd,0);
						delete ptp;
						continue;
					}

					event1.data.ptr = ptp;
					event1.events =
						(ptp->GetFlags() & PROTOCOL_RECV?EPOLLIN:0)|
						(ptp->GetFlags() & PROTOCOL_SEND?EPOLLOUT:0);
					epoll_ctl(efd,EPOLL_CTL_ADD,client.fd,&event1);
				}
			}else{
				Protocol::ClientProtocol *ptp = (Protocol::ClientProtocolHTTP *)events[i].data.ptr;
				Socket::ClientSocket client = ptp->GetSocket();

				uint sflags = ptp->GetFlags();

				if(sflags & PROTOCOL_RECV && events[i].events & EPOLLIN && ptp->GetStream()->Read()){
					switch(ptp->Poll(PROTOCOL_RECV)){
					case Protocol::ClientProtocol::POLL_RUN:
						taskq.push(ptp);
						break;
					case Protocol::ClientProtocol::POLL_CLOSE:
						epoll_ctl(efd,EPOLL_CTL_DEL,client.fd,0);
						delete ptp;
						continue;
					}
				}

				if(sflags & PROTOCOL_SEND && events[i].events & EPOLLOUT && ptp->GetStream()->Write()){
					switch(ptp->Poll(PROTOCOL_SEND)){
					case Protocol::ClientProtocol::POLL_RUN:
						taskq.push(ptp);
						break;
					case Protocol::ClientProtocol::POLL_CLOSE:
						epoll_ctl(efd,EPOLL_CTL_DEL,client.fd,0);
						delete ptp;
						continue;
					}
				}

				//dynamically enable both EPOLLIN and EPOLLOUT depending on the state
				if(sflags != ptp->GetFlags()){
					event1.data.ptr = ptp;
					event1.events =
						(ptp->GetFlags() & PROTOCOL_RECV?EPOLLIN:0)|
						(ptp->GetFlags() & PROTOCOL_SEND?EPOLLOUT:0);
					epoll_ctl(efd,EPOLL_CTL_MOD,client.fd,&event1);
				}
			}
		}

		//wait for parallel queue

		for(; !taskq.empty();){
			Protocol::ClientProtocol *ptp = taskq.front();
			uint sflags = ptp->GetFlags();

			if(!ptp->Run()){
				epoll_ctl(efd,EPOLL_CTL_DEL,ptp->GetSocket().fd,0);
				delete ptp;
			}else
			if(sflags != ptp->GetFlags()){
				event1.data.ptr = ptp;
				event1.events =
					(ptp->GetFlags() & PROTOCOL_RECV?EPOLLIN:0)|
					(ptp->GetFlags() & PROTOCOL_SEND?EPOLLOUT:0);
				epoll_ctl(efd,EPOLL_CTL_MOD,ptp->GetSocket().fd,&event1);
			}

			taskq.pop();
		}
	}

	return 0;
}

void DebugPrintf(const char *pfmt, ...){
	time_t rt;
	time(&rt);
	const struct tm *pti = localtime(&rt);

	char tbuf[256];
	strftime(tbuf,sizeof(tbuf),"%F %T",pti);
	printf("[tighttpd %s] ",tbuf);

	va_list args;
	va_start(args,pfmt);
	vprintf(pfmt,args);
	va_end(args);
}
