cmake_minimum_required( VERSION 3.8 )
project( GraphIPC )

set( CMAKE_C_STANDARD 99 )

find_package(PkgConfig REQUIRED)

# glib library stuff
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.23)
include_directories(${GLIB_INCLUDE_DIRS})
link_directories(${GLIB_LIBRARY_DIRS})

# Pthreads/Threads stuff
find_package( Threads )

add_library( RingBuffer lib/RingBuffer.c lib/RingBuffer.h)
add_library( GraphNetwork lib/GraphNetwork.c lib/GraphNetwork.h)
add_library( Assert lib/Assert.c lib/Assert.h)

add_executable( GraphWrap GraphWrap.c lib/utility.c lib/utility.h lib/GraphNetwork.h)
target_link_libraries( GraphWrap ${CMAKE_THREAD_LIBS_INIT} GraphNetwork RingBuffer Assert )

add_executable( ArgTest ArgTest.c )

add_executable( GraphRouter GraphRouter.c lib/utility.c lib/utility.h lib/GraphNetwork.h lib/Assert.c lib/Assert.h)
target_link_libraries( GraphRouter ${CMAKE_THREAD_LIBS_INIT} ${GLIB_LIBRARIES} GraphNetwork RingBuffer Assert )

add_executable( UnitTests UnitTests.c)
target_link_libraries( UnitTests RingBuffer ${CMAKE_THREAD_LIBS_INIT} Assert GraphNetwork )