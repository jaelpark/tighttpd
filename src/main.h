#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h> //strerror etc.

#include <tbb/tbb.h>

typedef unsigned int uint;

typedef std::basic_string<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>> tbb_string;
typedef std::basic_istringstream<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>> tbb_istringstream;

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
	tbb_string name;
	uint port;
	bool tls;
	//todo: certificates
	//client config variables
	tbb_string root;
	tbb_string resource;
	tbb_string mimetype;
	bool index;
	bool listing;
	bool cgi;
	//client constants
	tbb_string host;
	tbb_string uri;
	tbb_string referer;
	tbb_string address;
	tbb_string useragent;
	tbb_string accept;
	tbb_string acceptenc;
	tbb_string acceptlan;
};

void DebugPrintf(const char *, ...);

#endif
