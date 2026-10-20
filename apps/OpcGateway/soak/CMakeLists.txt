cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.Soak VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

find_package( open62541 REQUIRED )
findZlibAndJsonnet()

set( targetName Jde.Opc.Soak )
add_executable( ${targetName} )
compileOptions( ${targetName} )

#GatewayClientSocket comes from the test tree - the wire-protocol driver is shared, but no GTest and no embedded servers here.
target_sources( ${targetName} PRIVATE main.cpp SoakRunner.cpp ../tests/utils/GatewayClientSocket.cpp )

target_link_libraries( ${targetName} Jde.Opc.GatewayLib )
linkAppRuntime( ${targetName} NOMINMAX LIBXML2 )
target_precompile_headers( ${targetName} PRIVATE
	pc.h
)