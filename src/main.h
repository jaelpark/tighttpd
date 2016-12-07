#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h> //strerror etc.

#include <tbb/tbb.h>
//#include <Python.h>

typedef unsigned int uint;

#define SERVER_FLAG_TLS 0x1

class ServerInterface{
public:
	ServerInterface();
	virtual ~ServerInterface();
	virtual void Setup();
	virtual void Accept();
	void ResetConfig(); //reset client config
	//
	//server config variables
	//std::string software;
	std::string name;
	uint port;
	bool tls;
	//todo: certificates
	//client config variables
	std::string root;
	std::string mimetype;
	bool index;
	bool listing;
	bool cgi;
	//client constants
	std::string host;
	std::string uri;
	std::string resource;
	std::string referer;
	std::string address;
	std::string useragent;
	std::string accept;
	std::string acceptenc;
	std::string acceptlan;
};

void DebugPrintf(const char *, ...);

#endif
