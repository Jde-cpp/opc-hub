cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../build/functions.cmake )

#Jde.Opc.OpenProtocol's unit suite:  frames written from the specification's tables, each MID at each revision both ways,
#the specification's example frames, the refusals, and the fuzz target's corpus replayed;  and the session against the
#controller's side of the codec, in-process on loopback ports the system picks.  No server and no data source.
set( targetName Jde.Opc.OpenProtocol.Tests )
project( ${targetName} VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
message( ${targetName} )

find_package( GTest REQUIRED )
find_package( Threads REQUIRED )

if( NOT WIN32 )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		set( jsonnet_DIR ${CMAKE_INSTALL_PREFIX}/jsonnet CACHE PATH "Jsonnet directory" )
		link_directories( ${jsonnet_DIR}/lib )
	endif()
endif()

add_executable( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp ) #CONFIGURE_DEPENDS, as the other gtest suites:  a new *Tests.cpp is referenced by nothing else, so a stale glob would drop it silently.
file( GLOB headers CONFIGURE_DEPENDS *.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
target_compile_definitions( ${targetName} PRIVATE TESTS )

target_link_libraries( ${targetName} GTest::gtest Threads::Threads )
target_link_libraries( ${targetName} Jde.Opc.OpenProtocol Jde )

if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/OpenProtocol.Tests.jsonnet" )
	copyCommonDlls( Jde ) #no Jde.DB:  nothing here links or loads it.
else()
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} jsonnet++ libjsonnet.a ) #jsonnet
	endif()
endif()

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/OpenProtocol.Tests.jsonnet )

testPch( ${targetName} Jde.Opc.OpenProtocol )