cmake_minimum_required(VERSION 2.8)
project(tighttpd)

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_FLAGS "-std=c++11 -Wno-unused-parameter")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

file(GLOB SOURCES src/*.cpp)

add_executable(tighttpd ${SOURCES})
TARGET_LINK_LIBRARIES(tighttpd LINK_PUBLIC tbb tbbmalloc)
