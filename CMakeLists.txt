cmake_minimum_required(VERSION 2.8)
project(tighttpd)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_FLAGS "-std=c++11 -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

#set(Python_ADDITIONAL_VERSIONS 3.5)
#find_package(PythonLibs 3.5 REQUIRED)

#find_library(TBB_LIBS tbb tbbmalloc)

find_program(PYTHON_CONFIG NAMES python-config)
execute_process(COMMAND "${PYTHON_CONFIG}" --ldflags
	OUTPUT_VARIABLE PYTHON_LIBS)
string(STRIP ${PYTHON_LIBS} PYTHON_LIBS)

execute_process(COMMAND "${PYTHON_CONFIG}" --includes
	OUTPUT_VARIABLE PYTHON_INC)
string(STRIP ${PYTHON_INC} PYTHON_INC)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${PYTHON_INC}")

file(GLOB SOURCES src/*.cpp)

add_executable(tighttpd ${SOURCES})
TARGET_LINK_LIBRARIES(tighttpd LINK_PUBLIC tbb tbbmalloc ${PYTHON_LIBS} boost_python3)
#TARGET_LINK_LIBRARIES(tighttpd LINK_PUBLIC tbb tbbmalloc ${PYTHON_LIBRARIES})
