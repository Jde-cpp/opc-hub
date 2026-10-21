cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../build/functions.cmake )

#The PLC emulator's unit suite (emulator-review T2):  Signals (TagSpec parsing, the six generators), Quality (the 7.38
#status codes and their schedule), ParseDevices, and the PLC's own UA server - Jde.Opc.PlcEmulatorLib, which the emulator
#links too.  The rest of it (the app client, the OpcServer session) is what a unit must not need.  No database, no
#AppServer, no OpcServer.
set( targetName Jde.Opc.PlcEmulator.Tests )
project( ${targetName} VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

find_package( GTest REQUIRED )
find_package( open62541 REQUIRED )
findZlibAndJsonnet()

add_executable( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

target_link_libraries( ${targetName} GTest::gtest Jde.Opc.PlcEmulatorLib ) #which carries Jde.Opc and the nodeset loader.
if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.PlcEmulator.Tests.jsonnet" )
endif()
linkAppRuntime( ${targetName} NOMINMAX LIBXML2 )

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.PlcEmulator.Tests.jsonnet )

target_precompile_headers( ${targetName} PRIVATE pc.h )