#include "main.h"
#include "socket.h"
#include "protocol.h"

#include <csignal>
#include <stdlib.h>

#include <time.h> //logging gimmick
#include <stdarg.h>

#include <sys/epoll.h>

#ifdef USE_MT
#include <tbb/flow_graph.h>
#endif

ServerInterface::ServerInterface() : name("tighttpd"), port(8080), tls(false){
	//
}

ServerInterface::~ServerInterface(){
	//
}

void ServerInterface::Setup(){
	//
}

boost::python::object ServerInterface::Accept(){
	//
}

ClientInterface::ClientInterface(){
	//
}

ClientInterface::~ClientInterface(){
	//
}

void ClientInterface::ResetConfig(){
	root = ".";
	mimetype = "application/octet-stream";
	index = false;
	listing = true;
	cgi = false;
}

void ClientInterface::Setup(){
	//
}

//boost/python bindings for the server configuration
class PythonServerProxy : public ServerInterface, public boost::python::wrapper<ServerInterface>{
public:
	void Setup(){
		boost::python::override ovr = this->get_override("setup");
		if(ovr)
			ovr();
		else ServerInterface::Setup();
	}

	boost::python::object Accept(){
		boost::python::override ovr = this->get_override("accept");
		if(ovr)
			return ovr();
		return ServerInterface::Accept();
	}
};

class PythonClientProxy : public ClientInterface, public boost::python::wrapper<ClientInterface>{
public:
	void Setup(){
		boost::python::override ovr = this->get_override("setup");
		if(ovr)
			ovr();
		else ClientInterface::Setup();
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

struct tbb_string_to_object{
	static PyObject * convert(tbb_string const &s){
		return boost::python::incref(boost::python::object(s.c_str()).ptr());
	}
};

struct tbb_string_from_object{
	static void * convertible(PyObject *pyobj){
		return PyUnicode_Check(pyobj)?pyobj:0;
	}

	static void construct(PyObject *pyobj, boost::python::converter::rvalue_from_python_stage1_data *pdata){
		const char *pstr = PyUnicode_AsUTF8(pyobj);

		void *pdst = ((boost::python::converter::rvalue_from_python_storage<tbb_string> *)pdata)->storage.bytes;
		new(pdst) tbb_string(pstr);

		pdata->convertible = pdst;
	}
};

BOOST_PYTHON_MODULE(HttpModule){
	boost::python::to_python_converter<tbb_string,tbb_string_to_object>();
	boost::python::converter::registry::push_back(&tbb_string_from_object::convertible,&tbb_string_from_object::construct,
		boost::python::type_id<tbb_string>());

#define make_setter_s1(x) make_setter(&ServerInterface::x,boost::python::return_value_policy<boost::python::return_by_value>())
#define make_getter_s1(x) make_getter(&ServerInterface::x,boost::python::return_value_policy<boost::python::return_by_value>())
	boost::python::class_<PythonServerProxy,boost::noncopyable>("httpServer")
		.def("setup",&ServerInterface::Setup)
		.def("accept",&ServerInterface::Accept)
		//server config
		//.def_readwrite("software",&ServerInterface::software)
		.add_property("name",make_getter_s1(name),make_setter_s1(name))
		.def_readwrite("port",&ServerInterface::port)
		.def_readwrite("tls",&ServerInterface::tls)
		;
	boost::python::def("create",PythonServerManager::Create);

#define make_setter_c1(x) make_setter(&ClientInterface::x,boost::python::return_value_policy<boost::python::return_by_value>())
#define make_getter_c1(x) make_getter(&ClientInterface::x,boost::python::return_value_policy<boost::python::return_by_value>())
	boost::python::class_<PythonClientProxy,boost::noncopyable>("httpClient")
		.def("setup",&ClientInterface::Setup)
		.add_property("root",make_getter_c1(root),make_setter_c1(root))
		.add_property("resource",make_getter_c1(resource),make_setter_c1(resource))
		.add_property("mimetype",make_getter_c1(mimetype),make_setter_c1(mimetype))
		.add_property("cgibin",make_getter_c1(cgibin),make_setter_c1(cgibin))
		.add_property("cgiarg",make_getter_c1(cgiarg),make_setter_c1(cgiarg))
		.def_readwrite("index",&ClientInterface::index)
		.def_readwrite("listing",&ClientInterface::listing)
		.def_readwrite("cgi",&ClientInterface::cgi)
		//client constants
		.add_property("host",make_getter_c1(host))
		.add_property("uri",make_getter_c1(uri))
		.add_property("referer",make_getter_c1(referer))
		.add_property("address",make_getter_c1(address))
		.add_property("useragent",make_getter_c1(useragent))
		.add_property("accept",make_getter_c1(accept))
		.add_property("accept_encoding",make_getter_c1(acceptenc))
		.add_property("accept_language",make_getter_c1(acceptlan))
		;
}

int main(int argc, const char **pargv){
	if(argc < 2){
		DebugPrintf("Usage: tighttpd <config.py>\n");
		return -1;
	}

	wchar_t warg[1024];
	mbtowc(warg,pargv[0],sizeof(warg)/sizeof(warg[0]));
	Py_SetProgramName(warg);

	PyImport_AppendInittab("tighttpd",PyInit_HttpModule);
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

	std::queue<Protocol::ClientProtocol *> taskq; //task queue for intensive (parallelized) work
	for(;;){
		for(int n = epoll_wait(efd,events,MAX_EVENTS,-1), i = 0; i < n; ++i){
			//Find the corresponding server instance for this event, unless it's a client event
			PythonServerManager::SocketObjectPair *psop = 0;
			for(uint j = 0, m = PythonServerManager::slist.size(); j < m; ++j){
				if(events[i].data.ptr == &PythonServerManager::slist[j]){
					psop = &PythonServerManager::slist[j];
					break;
				}
			}
			if(psop){
				//Server socket event, incoming connection
				for(;;){
					Socket::ClientSocket client(psop->first.Accept());
					if(client.fd == -1)
						break;

					ServerInterface &psi = boost::python::extract<ServerInterface&>(psop->second)();

					//Extract the client interface returned by ServerInterface::Accept()
					boost::python::object clobj = psi.Accept();
					boost::python::extract<ClientInterface&> clextract(clobj);
					if(!clextract.check()){
						client.Close();
						break;
					}
					ClientInterface &pci = clextract();

					Protocol::ClientProtocolHTTP *ptp =
						new Protocol::ClientProtocolHTTP(client,&psi,&pci,clobj);

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

#ifdef USE_MT
		typedef std::pair<Protocol::ClientProtocol *, uint> FlowContent;
		tbb::flow::graph fg;
		tbb::flow::source_node<FlowContent> fgsrc(fg,[&](FlowContent &fc)->bool{
			if(taskq.empty())
				return false;

			Protocol::ClientProtocol *ptp = taskq.front();
			uint sflags = ptp->GetFlags();

			fc = FlowContent(ptp,sflags);
			taskq.pop();

			return true;
		});

		tbb::flow::function_node<FlowContent, FlowContent> fg_precfg(fg,tbb::flow::unlimited,[&](FlowContent fc)->FlowContent{
			fc.first->Accept();
			return fc;
		});

		tbb::flow::function_node<FlowContent, FlowContent> fg_setup(fg,1,[&](FlowContent fc)->FlowContent{
			fc.first->Configure();
			return fc;
		});

		tbb::flow::function_node<FlowContent, FlowContent> fg_postcfg(fg,tbb::flow::unlimited,[&](FlowContent fc)->FlowContent{
			fc.first->Process();
			return fc;
		});

		tbb::flow::function_node<FlowContent> fg_sflags(fg,1,[&](FlowContent fc)->void{
			Protocol::ClientProtocol *ptp = fc.first;
			uint sflags = fc.second;

			if(sflags != ptp->GetFlags()){
				event1.data.ptr = ptp;
				event1.events =
					(ptp->GetFlags() & PROTOCOL_RECV?EPOLLIN:0)|
					(ptp->GetFlags() & PROTOCOL_SEND?EPOLLOUT:0);
				epoll_ctl(efd,EPOLL_CTL_MOD,ptp->GetSocket().fd,&event1);
			}
		});

		tbb::flow::make_edge(fgsrc,fg_precfg);
		tbb::flow::make_edge(fg_precfg,fg_setup);
		tbb::flow::make_edge(fg_setup,fg_postcfg);
		tbb::flow::make_edge(fg_postcfg,fg_sflags);

		fgsrc.activate();
		fg.wait_for_all();


#else
		for(; !taskq.empty();){
			Protocol::ClientProtocol *ptp = taskq.front();
			uint sflags = ptp->GetFlags();

			ptp->Accept();
			ptp->Configure();
			ptp->Process();

			if(sflags != ptp->GetFlags()){
				event1.data.ptr = ptp;
				event1.events =
					(ptp->GetFlags() & PROTOCOL_RECV?EPOLLIN:0)|
					(ptp->GetFlags() & PROTOCOL_SEND?EPOLLOUT:0);
				epoll_ctl(efd,EPOLL_CTL_MOD,ptp->GetSocket().fd,&event1);
			}

			taskq.pop();
		}
#endif
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
