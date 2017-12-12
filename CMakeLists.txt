cmake_minimum_required( VERSION 3.8 )
project( GraphIPC )

set( CMAKE_C_STANDARD 99 )

find_package(PkgConfig REQUIRED)

# glib library stuff
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.23)
include_directories(${GLIB_INCLUDE_DIRS})
link_directories(${GLIB_LIBRARY_DIRS})

# libLFDS library stuff
include_directories( "/usr/lib/lfds/" )
#link_directories( "" )

find_package( Threads )

add_library( GraphNetwork GraphNetwork.c GraphNetwork.h )
add_library( RingBuffer RingBuffer.c RingBuffer.h )

add_executable( GraphWrap GraphWrap.c utility.c utility.h GraphNetwork.h )
target_link_libraries( GraphWrap GraphNetwork RingBuffer )

add_executable( ArgTest ArgTest.c )

add_executable( GraphRouter GraphRouter.c utility.c utility.h GraphNetwork.h )
target_link_libraries( GraphRouter ${CMAKE_THREAD_LIBS_INIT} ${GLIB_LIBRARIES} GraphNetwork RingBuffer )

add_executable( UnitTests UnitTests.c )
target_link_libraries( UnitTests RingBuffer ${CMAKE_THREAD_LIBS_INIT} )