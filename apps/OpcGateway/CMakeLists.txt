cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )

include( ${CMAKE_CURRENT_LIST_DIR}/../../build/functions.cmake )
message( "Jde.Opc.Gateway" )
project( Jde.Opc.Gateway VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
if( WIN32 )
	#OpenSSL is the vcpkg `x64-windows` (dynamic) triplet now - libcrypto-3-x64.dll / libssl-3-x64.dll.  Asking for static
	#here makes vcpkg-cmake-wrapper warn and FindOpenSSL restrict CMAKE_FIND_LIBRARY_SUFFIXES, which finds nothing.
endif()
find_package( OpenSSL REQUIRED )

find_package( open62541 REQUIRED )
if( WIN32 )
	add_definitions( -DZLIB_USE_STATIC_LIBS )
	set( ZLIB_ROOT ${CMAKE_INSTALL_PREFIX}/zlib )
else()
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		link_directories( ${JSONNET_DIR}/lib )
	endif()
endif()
find_package( ZLIB REQUIRED )

if( NOT jde_Superbuild )
	add_subdirectory( config/sql/sqlite )
	set( libDir ${jdeRoot}/libs )
	add_subdirectory( ${libDir}/fwk/src ../../libs/fwk/lib )
	add_subdirectory( ${libDir}/db/src ../../libs/db/lib )
	add_subdirectory( ${libDir}/db/drivers/sqlite/src ../../libs/db/drivers/sqlite/lib ) #the config/sql/sqlite MODULE above is only reachable through this driver
	add_subdirectory( ${libDir}/db/drivers/mysql/src ../../libs/db/drivers/mysql/lib )
	if( WIN32 ) #odbc is windows-only; mysql builds everywhere.
		add_subdirectory( ${libDir}/db/drivers/odbc/src ../../libs/db/drivers/odbc/lib )
	endif()
	add_subdirectory( ${libDir}/ql/src ../../libs/ql/lib )
	add_subdirectory( ${libDir}/access/src ../../libs/access/lib )
	add_subdirectory( ${libDir}/web/client ../../libs/web/client )
	add_subdirectory( ${libDir}/web/server ../../libs/web/server )
	add_subdirectory( ${libDir}/app/shared ../../libs/app/shared )
	add_subdirectory( ${libDir}/app/client ../../libs/app/client )
	add_subdirectory( ${libDir}/opc/src ../../libs/opc )
	add_subdirectory( ${libDir}/historian/src ../../libs/historian/lib ) #Jde.Opc.GatewayLib links it.
	add_subdirectory( src lib )

	if( jde_TESTS )
		enable_testing()
		add_subdirectory( tests )
	endif()
endif()

set( targetName Jde.Opc.Gateway )

#message( ${JSONNET_DIR}/lib )
#link_directories( ${JSONNET_DIR}/lib )
#add_definitions( -DUA_ENABLE_ENCRYPTION )

add_executable( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp src/main.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )
if( WIN32)
	list( APPEND sources src/OpcGateway.rc )
endif()
target_sources( ${targetName} PRIVATE ${sources} ${resources} ${headers} )

if( WIN32 )
#	target_link_libraries( ${targetName} ZLIB::ZLIB )
	target_link_libraries( ${targetName} Iphlpapi.lib )
else()
	target_link_libraries( ${targetName} stdc++exp )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} libjsonnet++.so libjsonnet.so )
	endif()
endif()
target_link_libraries( ${targetName} OpenSSL::SSL )
target_link_libraries( ${targetName} Jde.Opc.GatewayLib )

#Single TU (src/main.cpp), but the PUBLIC pc.h of Jde.Web.Client et al. propagates a ~116MB PCH into
#this target - a PCH that costs more to build than the one file it serves.  Jde.App.Server inherits the
#same header set (identical modulo include order) with identical defines and flags, so reuse its PCH
#rather than building a second copy.  Guarded: a standalone (non-superbuild) configure of this app
#does not define Jde.App.Server, and then the target keeps its own PCH.
if( TARGET Jde.App.Server )
	target_precompile_headers( ${targetName} REUSE_FROM Jde.App.Server )
endif()

linkConfigFile( "${jdeRoot}/libs/db/config/common-meta.libsonnet" "${CMAKE_CURRENT_SOURCE_DIR}/config/common-meta.libsonnet" )
if( WIN32 )
	set( appOutDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}" )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-c -tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Gateway.jsonnet"
		RUNTIME_OUTPUT_DIRECTORY "${appOutDir}"
		PDB_OUTPUT_DIRECTORY "${appOutDir}"
	)
	copyCommonDlls()
endif()