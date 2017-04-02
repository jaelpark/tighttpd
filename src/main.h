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

#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h> //strerror etc.

#include <deque>
#include <queue>

#include <tbb/tbb.h>

#include <boost/python.hpp>

/*
Global definitions and configuration interfaces.
2.4.2017
*/

typedef unsigned int uint;

typedef std::basic_string<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>> tbb_string;
typedef std::basic_istringstream<char,std::char_traits<char>,tbb::cache_aligned_allocator<char>> tbb_istringstream;

class ServerInterface{
public:
	ServerInterface();
	virtual ~ServerInterface();
	virtual void Setup();
	virtual boost::python::object Accept();
	//
	//std::string software;
	tbb_string name;
	uint port;
	bool tls;
};

class ClientInterface{
public:
	ClientInterface();
	virtual ~ClientInterface();
	virtual void Setup();
	void ResetConfig(); //reset client config
	//
	tbb_string root;
	tbb_string resource;
	tbb_string mimetype;
	tbb_string cgibin;
	tbb_string cgiarg;
	bool index;
	bool listing;
	bool deny;
	bool cgi;
	//client constants
	tbb_string host;
	tbb_string uri;
	tbb_string referer;
	tbb_string cookie;
	tbb_string address;
	tbb_string useragent;
	tbb_string accept;
	tbb_string acceptenc;
	tbb_string acceptlan;
};

void DebugPrintf(const char *, ...);

#endif
