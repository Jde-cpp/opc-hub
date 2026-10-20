cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.PlcEmulator VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

find_package( open62541 REQUIRED )
find_package( NodesetLoader REQUIRED ) #the emulated PLC's own address space is the same NodeSet2 file the OpcServer loads.
if( WIN32 AND NOT jde_REPOS )
	set( LIBXML2_LIBRARY ${CMAKE_INSTALL_PREFIX}/libxml2/lib/libxml2.lib CACHE FILEPATH "LibXml2 library" )
	set( LIBXML2_INCLUDE_DIR ${CMAKE_INSTALL_PREFIX}/libxml2/include/libxml2 CACHE PATH "LibXml2 headers" )
endif()
find_package( LibXml2 REQUIRED )
findZlibAndJsonnet()

set( targetName Jde.Opc.PlcEmulator )
add_executable( ${targetName} )
compileOptions( ${targetName} )

target_sources( ${targetName} PRIVATE main.cpp EmulatorAppClient.cpp EmulatorClient.cpp PlcServer.cpp Quality.cpp Signals.cpp Devices.cpp Emulator.cpp )
if( NOT WIN32 )
	target_include_directories( ${targetName} PRIVATE /usr/include/libxml2 )
endif()

#Jde.Opc carries the UA client/PubSub pieces; no gateway lib - the emulator is a foreign device, not a gateway.
target_link_libraries( ${targetName} Jde Jde.QL Jde.App.Shared Jde.App.Client Jde.Web.Client Jde.Opc open62541::open62541 NodesetLoader LibXml2::LibXml2 )
if( WIN32 )
	target_link_libraries( ${targetName} ${CMAKE_INSTALL_PREFIX}/protobuf/lib/utf8_range.lib ) #protobufd.lib comes in through Jde.App.Shared as a plain path, so nothing carries its utf8_range dependency; the other exes inherit it from Jde.Web.Server, which a client-only device does not link.
else()
	target_link_libraries( ${targetName} OpenSSL::SSL protobuf::libprotobuf ) #IAppClient's proto headers; ServerLib links it the same way.
endif()
linkAppRuntime( ${targetName} NOMINMAX LIBXML2 )
target_precompile_headers( ${targetName} PRIVATE pc.h )