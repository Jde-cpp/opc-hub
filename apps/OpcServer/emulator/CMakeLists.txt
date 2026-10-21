cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.PlcEmulator VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

find_package( open62541 REQUIRED )
findNodesetLoader() #the emulated PLC's own address space is the same NodeSet2 file the OpcServer loads.
findZlibAndJsonnet()

#The PLC's units that need no app client - its tags (Quality, Signals, Devices) and its own UA server (PlcServer) - compiled
#once for the emulator, its unit suite and Jde.Opc.Server.Tests, whose PubSubTests publish through PlcServer.  On Jde.Opc's
#PCH, whose defines it matches:  the emulator's own carries the app protos, which the unit suite links nothing for.
set( libName Jde.Opc.PlcEmulatorLib )
add_library( ${libName} )
compileOptions( ${libName} )
target_sources( ${libName} PRIVATE Quality.cpp Signals.cpp Devices.cpp PlcServer.cpp Quality.h Signals.h Devices.h PlcServer.h )
if( WIN32 )
	target_compile_definitions( ${libName} PRIVATE NOMINMAX )
endif()
target_link_libraries( ${libName} Jde.Opc )
linkNodesetLoader( ${libName} )
target_precompile_headers( ${libName} REUSE_FROM Jde.Opc )

set( targetName Jde.Opc.PlcEmulator )
add_executable( ${targetName} )
compileOptions( ${targetName} )

target_sources( ${targetName} PRIVATE main.cpp EmulatorAppClient.cpp EmulatorClient.cpp Emulator.cpp )

#Jde.Opc carries the UA client/PubSub pieces; no gateway lib - the emulator is a foreign device, not a gateway.
target_link_libraries( ${targetName} Jde Jde.QL Jde.App.Shared Jde.App.Client Jde.Web.Client Jde.Opc open62541::open62541 ${libName} )
linkAppRuntime( ${targetName} NOMINMAX LIBXML2 )
target_precompile_headers( ${targetName} PRIVATE pc.h )