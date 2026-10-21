cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../build/functions.cmake )

#The PLC emulator's unit suite (emulator-review T2):  Signals (TagSpec parsing, the six generators), Quality (the 7.38
#status codes and their schedule), ParseDevices, and the PLC's own UA server.  Those sources compile straight into this target - the emulator is an exe, and the rest
#of it (the app client, the OpcServer session) is what a unit must not need.  No database, no AppServer, no OpcServer.
set( targetName Jde.Opc.PlcEmulator.Tests )
project( ${targetName} VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
message( ${targetName} )

find_package( GTest REQUIRED )
find_package( Threads REQUIRED )
find_package( OpenSSL REQUIRED )
find_package( open62541 REQUIRED )
find_package( NodesetLoader REQUIRED ) #PlcServer loads the pumps NodeSet2, as the emulator does.
if( WIN32 AND NOT jde_REPOS )
	set( LIBXML2_LIBRARY ${CMAKE_INSTALL_PREFIX}/libxml2/lib/libxml2.lib CACHE FILEPATH "LibXml2 library" )
	set( LIBXML2_INCLUDE_DIR ${CMAKE_INSTALL_PREFIX}/libxml2/include/libxml2 CACHE PATH "LibXml2 headers" )
endif()
find_package( LibXml2 REQUIRED )
findZlibAndJsonnet()

add_executable( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )
list( APPEND sources ../Quality.cpp ../Signals.cpp ../Devices.cpp ../PlcServer.cpp )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
target_compile_definitions( ${targetName} PRIVATE TESTS )
if( NOT WIN32 )
	target_include_directories( ${targetName} PRIVATE /usr/include/libxml2 )
endif()

target_link_libraries( ${targetName} GTest::gtest Threads::Threads )
target_link_libraries( ${targetName} Jde.Opc Jde.QL Jde.DB Jde open62541::open62541 NodesetLoader LibXml2::LibXml2 fmt::fmt )
target_link_libraries( ${targetName} protobuf::libprotobuf ) #through Jde.Opc's uatypes, as libs/opc/tests explains.
if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.PlcEmulator.Tests.jsonnet" )
else()
	target_link_libraries( ${targetName} OpenSSL::SSL )
endif()
linkAppRuntime( ${targetName} NOMINMAX LIBXML2 )

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.PlcEmulator.Tests.jsonnet )

target_precompile_headers( ${targetName} PRIVATE pc.h )