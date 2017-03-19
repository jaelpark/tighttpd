cmake_minimum_required(VERSION 2.8.10)
project(tighttpd)

if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 5.1)
	message(FATAL_ERROR "GCC 5.1 (minimum) required")
endif()

if(NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE "Release")
endif()

set(CMAKE_CXX_FLAGS "-std=c++11 -Wno-unused-parameter -Wno-unused-result")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

set(Python_ADDITIONAL_VERSIONS 3.4 3.5 3.6)
find_package(PythonLibs 3 REQUIRED)
include_directories(${PYTHON_INCLUDE_DIRS})

STRING(REGEX REPLACE "[^0-9]" "" py_version_postfix ${PYTHONLIBS_VERSION_STRING})
STRING(REGEX REPLACE "[0-9]$" "" py_version_postfix ${py_version_postfix})
find_package(Boost COMPONENTS python-py${py_version_postfix})

if(NOT Boost_PYTHON-PY${py_version_postfix}_FOUND)
	find_package(Boost COMPONENTS python3 REQUIRED)
endif()

include_directories(${Boost_INCLUDE_DIR})

file(GLOB SOURCES src/*.cpp)

add_executable(tighttpd ${SOURCES})
TARGET_LINK_LIBRARIES(tighttpd LINK_PUBLIC tbb tbbmalloc ${PYTHON_LIBRARIES} ${Boost_LIBRARIES})
