cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )

#Jde.Opc.Tests is already taken by apps/OpcGateway/tests - the gateway/server integration suite, which stands up an
#embedded AppServer/OpcServer.  This is Jde.Opc's own unit suite: uatypes round trips, no server, no data source.
set( targetName Jde.Opc.Lib.Tests )
project( ${targetName} VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
message( ${targetName} )

find_package( GTest REQUIRED )
find_package( Threads REQUIRED )
find_package( OpenSSL REQUIRED )
find_package( open62541 REQUIRED )

if( NOT WIN32 )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		set( jsonnet_DIR ${CMAKE_INSTALL_PREFIX}/jsonnet CACHE PATH "Jsonnet directory" )
		link_directories( ${jsonnet_DIR}/lib )
	endif()
endif()

add_executable( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
target_compile_definitions( ${targetName} PRIVATE TESTS )
if( WIN32 )
	target_compile_definitions( ${targetName} PRIVATE NOMINMAX )
endif()

target_link_libraries( ${targetName} GTest::gtest Threads::Threads )
target_link_libraries( ${targetName} Jde.Opc Jde.QL Jde ) #Jde.QL: NodeId::DecodeJson/ParseQL parse through QL::Parser.  No driver - nothing here opens a data source.
target_link_libraries( ${targetName} open62541::open62541 )
target_link_libraries( ${targetName} fmt::fmt )

#UADateTime returns google::protobuf::Timestamp/Duration, so the protobuf runtime is needed here and not only inside
#Jde.Opc.  The imported target on both platforms, not the raw .lib path libs/opc/src uses: protobuf 35's generated code
#pulls absl, and only protobuf::libprotobuf carries that dependency chain (same reason apps/OpcServer/src names it).
target_link_libraries( ${targetName} protobuf::libprotobuf )

if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Lib.Tests.jsonnet" )
	copyCommonDlls()
else()
	target_link_libraries( ${targetName} OpenSSL::SSL )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} jsonnet++ libjsonnet.a ) #jsonnet
	endif()
endif()

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Lib.Tests.jsonnet )

target_precompile_headers( ${targetName} PRIVATE pc.h )
