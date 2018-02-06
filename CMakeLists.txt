cmake_minimum_required( VERSION 3.8 )
project( GraphIPC )

set( CMAKE_C_STANDARD 99 )

find_package(PkgConfig REQUIRED)

# Doxygen Support
find_package( Doxygen REQUIRED dot )

if( DOXYGEN_FOUND )
    set( DOXYGEN_GENERATE_HTML YES )
    set( DOXYGEN_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/doc )

    doxygen_add_docs( doxygen ${PROJECT_SOURCE_DIR} COMMENT "Generate HTML documentation" )
endif( DOXYGEN_FOUND )

# glib library stuff
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.23)
include_directories(${GLIB_INCLUDE_DIRS})
link_directories(${GLIB_LIBRARY_DIRS})

# Pthreads/Threads stuff
find_package( Threads )

add_library( DataStructures lib/RingBuffer.c lib/RingBuffer.h lib/LinkedList.c lib/LinkedList.h lib/BTree.c lib/BTree.h)
add_library( GraphNetwork lib/GraphNetwork.c lib/GraphNetwork.h lib/AddressTrie.c lib/AddressTrie.h )
target_link_libraries( GraphNetwork m )
add_library( Assert lib/Assert.c lib/Assert.h )

add_executable( Graph GraphWrap.c lib/utility.c lib/utility.h lib/GraphNetwork.h common.c common.h )
target_link_libraries( Graph ${CMAKE_THREAD_LIBS_INIT} GraphNetwork DataStructures Assert )

add_executable( GraphRouter GraphRouter.c lib/utility.c lib/utility.h lib/GraphNetwork.h lib/Assert.c lib/Assert.h common.c common.h )
target_link_libraries( GraphRouter ${CMAKE_THREAD_LIBS_INIT} ${GLIB_LIBRARIES} GraphNetwork DataStructures Assert )

add_executable( ArgTest ArgTest.c )

add_executable( UnitTests UnitTests.c lib/utility.h lib/utility.c )
target_link_libraries( UnitTests DataStructures ${CMAKE_THREAD_LIBS_INIT} Assert GraphNetwork )