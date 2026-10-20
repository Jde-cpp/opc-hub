cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.HubLib VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

#The composition of Jde.App.ServerLib + Jde.Opc.GatewayLib into one process (hubStartup.cpp) and the gateway's in-process
#app client (HubAppClient.cpp).  A library so Jde.Opc.Hub.Tests embeds exactly what the exe runs, as the two app libs do.
set( targetName Jde.Opc.HubLib )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp ql/*.cpp )
get_filename_component( main_cpp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ABSOLUTE )
list( REMOVE_ITEM sources ${main_cpp} )
file( GLOB headers CONFIGURE_DEPENDS *.h ql/*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

dbDriverDependencies( ${targetName} ) #the sqlite procs come with the two app libs.

target_link_libraries( ${targetName} Jde.App.ServerLib Jde.Opc.GatewayLib )

#The gateway lib's PCH (open62541, the Opc protos, its usings and exception type) is what the gateway headers included here
#assume, and the two libs compile with the same defines and flags.  Unguarded: this lib links that one, so it always exists.
target_precompile_headers( ${targetName} REUSE_FROM Jde.Opc.GatewayLib )