#ifndef MAIN_H
#define MAIN_H

#include <stdio.h>
#include <string.h> //strerror etc.

#include <tbb/tbb.h>
#include <python3.5m/Python.h>

typedef unsigned int uint;

namespace Config{

class File{
public:
	File();
	~File();
	const char * Read(const char *);
	void Free();
private:
	char *pdata;
};

}

void DebugPrintf(const char *, ...);

#endif
