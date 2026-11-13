cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
message( Jde.Opc.FlowLib )
project( Jde.Opc.FlowLib VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
find_package( open62541 REQUIRED )
find_package( qjs CONFIG REQUIRED GLOBAL ) #quickjs-ng, the Function node's engine (src/js).  GLOBAL:  the library is static, so what links it links qjs, from its own directory.

#Flows (docs/flows/spec.md *Placement*):  the graph, the engine on a strand per flow, the stock nodes and the Function
#node's sandbox, with no gateway and no database.  Jde.Opc.GatewayLib is to host it.  Static, as the historian is, so no exports header.
set( targetName Jde.Opc.FlowLib )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp )
file( GLOB_RECURSE headers CONFIGURE_DEPENDS *.h ${jdeRoot}/include/jde/flows/*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
if( WIN32 )
	target_compile_definitions( ${targetName} PRIVATE NOMINMAX )
endif()

#Jde.Opc.HistorianLib for IClock;  Jde.Opc for Value;  qjs for the Function node's sandbox.
target_link_libraries( ${targetName} Jde.Opc.HistorianLib Jde.Opc qjs )
target_precompile_headers( ${targetName} PRIVATE pc.h )