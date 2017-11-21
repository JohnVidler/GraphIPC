cmake_minimum_required(VERSION 3.8)
project(GraphIPC)

set(CMAKE_C_STANDARD 99)

find_package( Threads )

add_executable( GraphWrap GraphWrap.c utility.c GraphNetwork.h )

add_executable( ArgTest ArgTest.c )

add_executable( GraphRouter GraphRouter.c utility.c GraphNetwork.h )
target_link_libraries( GraphRouter ${CMAKE_THREAD_LIBS_INIT} )