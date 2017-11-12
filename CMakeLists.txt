cmake_minimum_required(VERSION 3.8)
project(GraphIPC)

set(CMAKE_C_STANDARD 99)

set(SOURCE_FILES GraphWrap.c utility.c utility.h)
add_executable( GraphWrap ${SOURCE_FILES} )

add_executable( ArgTest ArgTest.c )