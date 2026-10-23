cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../build/functions.cmake )
message( Jde.Opc.UAServer )
project( Jde.Opc.UAServer VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
find_package( open62541 REQUIRED )

#The UA server core:  the endpoint, its trust list, the session login, node rights and the history backend, which the
#OpcServer links and the gateway's endpoint will (docs/servers/spec.md *The server library*).
set( targetName Jde.Opc.UAServer )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h ${jdeRoot}/include/jde/opc/server/*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
if( WIN32 )
	target_compile_definitions( ${targetName} PRIVATE NOMINMAX )
endif()

target_link_libraries( ${targetName} PUBLIC Jde.App.Client Jde.Opc Jde.Opc.HistorianLib )
target_precompile_headers( ${targetName} PRIVATE pc.h )