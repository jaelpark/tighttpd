#include "main.h"
#include "socket.h"
#include "protocol.h"

#include <csignal>
#include <stdlib.h>

#include <time.h> //logging gimmick
#include <stdarg.h>

#include <sys/epoll.h>

//#include <tbb/flow_graph.h>

namespace Config{

File::File(){
	//
}

File::~File(){
	//
}

const char * File::Read(const char *psrc){
	FILE *pf = fopen(psrc,"rb");
	if(!pf)
		return 0;
	fseek(pf,0,SEEK_END);
	size_t len = ftell(pf);
	fseek(pf,0,SEEK_SET);

	pdata = new char[len+1];
	fread(pdata,1,len,pf);
	pdata[len] = 0;

	fclose(pf);
	return pdata;
}

void File::Free(){
	delete pdata;
}

}

int main(int argc, const char **pargv){
	if(argc < 3){
		DebugPrintf("Usage: tighttpd <config.py> <port>\n");
		return -1;
	}

	Config::File cfg;
	const char *pcfgsrc = cfg.Read(pargv[1]);
	if(!pcfgsrc){
		DebugPrintf("Error: unable to open %s\n",pargv[1]);
		return -1;
	}

	wchar_t warg[1024];
	mbtowc(warg,pargv[0],sizeof(warg)/sizeof(warg[0]));
	Py_SetProgramName(warg);

	PyImport_AppendInittab("tighttpd",[]()->PyObject *{
		static struct PyModuleDef tld = {PyModuleDef_HEAD_INIT,"tighttpd","doc",-1,0,0,0,0,0};
		return PyModule_Create(&tld);
	});

	Py_Initialize();

	PyObject *pmod = PyImport_AddModule("tighttpd");
	if(!Protocol::ClientProtocolHTTP::InitConfigModule(pmod,pcfgsrc)){
		Py_Finalize();
		return -1;
	}

	cfg.Free();

	signal(SIGINT,[](int s)->void{
		DebugPrintf("Received SIGINT\n");

		Py_Finalize();
		exit(0);
	});

	Socket::ServerSocket server(0);
	int sfd = server.Listen(pargv[2]);

	//Create the epoll socket monitoring instance
	//-> man epoll
#define MAX_EVENTS 1024
	struct epoll_event event1, events[MAX_EVENTS];
	int efd = epoll_create1(0);
	if(efd == -1){
		DebugPrintf("Error: epoll_create\n");
		return -1;
	}

	event1.data.ptr = &server;
	event1.events = EPOLLIN;
	epoll_ctl(efd,EPOLL_CTL_ADD,sfd,&event1);

	std::queue<Protocol::ClientProtocol *> taskq; //task queue for intensive work
	for(;;){
		//Run() parallel queue
		//serial node for python execution?
		//also, final epoll_ctl must be serial and somehow synchronized with the loop below (spin mutex for each protocol class?)
		//TODO: if work queue is empty, block indefinitely (-1). Otherwise return immediately (0).
		for(int n = epoll_wait(efd,events,MAX_EVENTS,-1), i = 0; i < n; ++i){
			if(events[i].data.ptr == &server){
				for(;;){
					Socket::ClientSocket client(server.Accept());
					if(client.fd == -1)
						break;

					Protocol::ClientProtocolHTTP *ptp =
						new Protocol::ClientProtocolHTTP(client);

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

			ptp->Run();

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
	strftime(tbuf,sizeof(tbuf),"[tighttpd %F %T]",pti);
	printf("%s ",tbuf);

	va_list args;
	va_start(args,pfmt);
	vprintf(pfmt,args);
	va_end(args);
}
