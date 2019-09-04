cmake_minimum_required( VERSION 3.8 )
project( GraphIPC )

set( CMAKE_C_STANDARD 99 )
set( CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall" )

find_package(PkgConfig REQUIRED)

# Doxygen Support
find_package( Doxygen REQUIRED dot )

if( DOXYGEN_FOUND )
    set( DOXYGEN_GENERATE_HTML YES )
    set( DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/doc )

    doxygen_add_docs( doxygen ${PROJECT_SOURCE_DIR} COMMENT "Generate HTML documentation" )
endif( DOXYGEN_FOUND )

# glib library stuff
pkg_check_modules(GLIB REQUIRED glib-2.0>=2.23)
include_directories(${GLIB_INCLUDE_DIRS})
link_directories(${GLIB_LIBRARY_DIRS})

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

add_library( Common Log.c Log.h )

add_library( DataStructures lib/RingBuffer.c lib/RingBuffer.h lib/LinkedList.c lib/LinkedList.h lib/avl.c lib/avl.h)

add_library( GraphNetwork lib/GraphNetwork.c lib/GraphNetwork.h lib/packet.c IndexTable.c IndexTable.h NodeTable.c NodeTable.h ForwardTable.h ForwardTable.c )
target_link_libraries( GraphNetwork m DataStructures )

add_library( Assert lib/Assert.c lib/Assert.h )

add_executable( Graph GraphWrap.c lib/utility.c lib/utility.h lib/GraphNetwork.h common.c common.h )
target_link_libraries( Graph ${CMAKE_THREAD_LIBS_INIT} Common GraphNetwork DataStructures Assert )

add_executable( GraphRouter GraphRouter.c lib/utility.c lib/utility.h lib/GraphNetwork.h lib/Assert.c lib/Assert.h common.c common.h Log.c Log.h)
target_link_libraries( GraphRouter ${CMAKE_THREAD_LIBS_INIT} Common GraphNetwork DataStructures Assert )

add_executable( ArgTest ArgTest.c )
target_link_libraries( ArgTest Common )

add_executable( UnitTests UnitTests.c lib/utility.h lib/utility.c )
target_link_libraries( UnitTests Common DataStructures ${CMAKE_THREAD_LIBS_INIT} Assert GraphNetwork )