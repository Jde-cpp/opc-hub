cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.Hub.Tests VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

find_package( open62541 REQUIRED )
findZlibAndJsonnet()

#The hub's composition (Jde.Opc.HubLib) embedded the way the exe runs it, plus an embedded OpcServer for the OPC path - as
#Jde.Opc.Tests, on its own ports (1973-1975/4975) so it never waits on a live hub, service or the other suites.
set( targetName Jde.Opc.Hub.Tests )
add_executable( ${targetName} )
compileOptions( ${targetName} )

file( GLOB sources CONFIGURE_DEPENDS *.cpp )
list( APPEND sources ${CMAKE_CURRENT_SOURCE_DIR}/../../OpcGateway/tests/utils/helpers.cpp ) #the gateway suite's server-connection helpers;  its socket client is Jde.Opc.TestUtils.
if( WIN32 )
	list( APPEND sources OpcHubTests.rc )
	jdeVersionResource( ${targetName} ) #the .rc's version values from JDE_VERSION (build/functions.cmake)
endif()
dbDriverDependencies( ${targetName} Jde.DB.Sqlite.AppServer Jde.DB.Sqlite.OpcGateway ) #the ctest run below dlopens these via config/args/sqlite
file( GLOB headers CONFIGURE_DEPENDS *.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

if( NOT jde_Superbuild ) #what ../CMakeLists.txt does not add:  it has the app libs and the hub lib.
	if( NOT TARGET Jde.Opc.HistorianLib ) #Jde.Opc.UAServer links it.
		add_subdirectory( ${jdeRoot}/libs/historian/src ../../../libs/historian/lib )
	endif()
	if( NOT TARGET Jde.Opc.UAServer ) #Jde.Opc.ServerLib links it.
		add_subdirectory( ${jdeRoot}/libs/opc/server/src ../../../libs/opc/server/lib )
	endif()
	add_subdirectory( ../../OpcServer/src ../../../apps/OpcServer/lib )
	add_subdirectory( ../../OpcGateway/tests/utils ../../../apps/OpcGateway/tests/utils )
endif()

find_package( GTest REQUIRED )
target_link_libraries( ${targetName} GTest::gtest )

target_link_libraries( ${targetName} Jde.Opc.HubLib Jde.Opc.ServerLib Jde.Opc.TestUtils ) #the hub lib carries the two app libs and everything under them; the server lib is the embedded OpcServer's.
linkAppRuntime( ${targetName} NOMINMAX LIBXML2 )

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Hub.Tests.jsonnet )

if( WIN32 )
	set( appOutDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}" )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Hub.Tests.jsonnet"
		RUNTIME_OUTPUT_DIRECTORY "${appOutDir}"
	)
endif()
target_precompile_headers( ${targetName} PRIVATE
	pc.h
)