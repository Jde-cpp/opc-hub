cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../build/functions.cmake )
project( Jde.Opc.TestUtils VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

#GatewayClientSocket, the gateway's wire-protocol client, compiled once for the three that drive a gateway with it:  the soak,
#Jde.Opc.Tests and Jde.Opc.Hub.Tests.  No gtest, which the soak does not link - so helpers.cpp, with its ADD_FAILURE, stays
#with the two suites.
set( targetName Jde.Opc.TestUtils )
add_library( ${targetName} )
compileOptions( ${targetName} )
target_sources( ${targetName} PRIVATE GatewayClientSocket.cpp GatewayClientSocket.h )
target_link_libraries( ${targetName} Jde.Opc.GatewayLib )
target_precompile_headers( ${targetName} REUSE_FROM Jde.Opc.GatewayLib ) #as Jde.Opc.HubLib:  the same defines and flags, and the gateway headers it includes assume that PCH