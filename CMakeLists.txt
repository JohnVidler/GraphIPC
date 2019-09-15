cmake_minimum_required( VERSION 3.8 )
project( GraphIPC )

set( CMAKE_C_STANDARD 99 )
set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -ggdb" )

find_package( PkgConfig REQUIRED )

# Doxygen Support
find_package( Doxygen REQUIRED dot )

if( DOXYGEN_FOUND )
    set( DOXYGEN_GENERATE_HTML YES )
    set( DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/doc )

    doxygen_add_docs( doxygen ${PROJECT_SOURCE_DIR} COMMENT "Generate HTML documentation" )
endif( DOXYGEN_FOUND )

# glib library stuff
pkg_check_modules( GLIB REQUIRED glib-2.0>=2.23 )
include_directories( ${GLIB_INCLUDE_DIRS} )
link_directories( ${GLIB_LIBRARY_DIRS} )

# Pthreads/Threads stuff
find_package( Threads )

# Build specific data to build dynamic includes
execute_process( COMMAND "git rev-parse --short HEAD" OUTPUT_VARIABLE GIT_SHORT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE )
add_definitions( -DGIT_SHORT_HASH=${GIT_SHORT_HASH} )

execute_process( COMMAND "git rev-parse HEAD" OUTPUT_VARIABLE GIT_HASH OUTPUT_STRIP_TRAILING_WHITESPACE )
add_definitions( -DGIT_HASH=${GIT_HASH} )

execute_process( COMMAND "git name-rev --tags --name-only $(git rev-parse HEAD)" OUTPUT_VARIABLE GIT_TAG OUTPUT_STRIP_TRAILING_WHITESPACE )
add_definitions( -DGIT_TAG=${GIT_TAG} )


configure_file( BuildInfo.h.in ${CMAKE_BINARY_DIR}/generated/BuildInfo.h )
include_directories( ${CMAKE_BINARY_DIR}/generated/ )

add_library( klib
        lib/klib/bgzf.c lib/klib/bgzf.h
        lib/klib/kalloc.c lib/klib/kalloc.h
        lib/klib/kavl.h
        lib/klib/kbit.h
        lib/klib/kbtree.h
        lib/klib/kdq.h
        lib/klib/keigen.c lib/klib/keigen.h
        lib/klib/ketopt.h
        lib/klib/kexpr.c lib/klib/kexpr.h
        lib/klib/kgraph.h
        lib/klib/khash.h
        lib/klib/khmm.c lib/klib/khmm.h
        lib/klib/klist.h
        lib/klib/kmath.c lib/klib/kmath.h
        lib/klib/knetfile.c lib/klib/knetfile.h
        lib/klib/knhx.c lib/klib/knhx.h
        lib/klib/kopen.c
        lib/klib/krng.h
        lib/klib/ksa.c
        lib/klib/kseq.h
        lib/klib/kson.c lib/klib/kson.h
        lib/klib/ksort.h
        lib/klib/kstring.c lib/klib/kstring.h
        lib/klib/ksw.c lib/klib/ksw.h
        lib/klib/kthread.c lib/klib/kthread.h
        lib/klib/kurl.c lib/klib/kurl.h
        lib/klib/kvec.h
)

add_library( Common Log.c Log.h )

add_library( DataStructures lib/RingBuffer.c lib/RingBuffer.h lib/LinkedList.c lib/LinkedList.h lib/avl.c lib/avl.h)

add_library( GraphNetwork lib/GraphNetwork.c lib/GraphNetwork.h lib/packet.c IndexTable.c IndexTable.h NodeTable.c NodeTable.h ForwardTable.h ForwardTable.c )
target_link_libraries( GraphNetwork m DataStructures )

add_library( Assert lib/Assert.c lib/Assert.h )

add_executable( Graph GraphWrap.c lib/utility.c lib/utility.h lib/GraphNetwork.h common.c common.h )
target_link_libraries( Graph ${CMAKE_THREAD_LIBS_INIT} Common GraphNetwork DataStructures Assert )

add_executable( GraphRouter GraphRouter.c lib/utility.c lib/utility.h lib/GraphNetwork.h lib/Assert.c lib/Assert.h common.c common.h Log.c Log.h)
target_link_libraries( GraphRouter ${CMAKE_THREAD_LIBS_INIT} Common GraphNetwork DataStructures Assert klib )

add_executable( ArgTest ArgTest.c )
target_link_libraries( ArgTest Common )

add_executable( UnitTests UnitTests.c lib/utility.h lib/utility.c )
target_link_libraries( UnitTests Common DataStructures ${CMAKE_THREAD_LIBS_INIT} Assert GraphNetwork )