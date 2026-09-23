cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )

include( ${CMAKE_CURRENT_LIST_DIR}/../../build/functions.cmake )
message( "Jde.Opc.Hub" )
project( Jde.Opc.Hub VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
#The AppServer and the OpcGateway in one process (apps/OpcHub/src/hubStartup.cpp) - the two existing app libs linked into one
#exe, so Jde.App.Server and Jde.Opc.Gateway keep building for split deployments.  Dependency lines follow the gateway exe's:
#linking Jde.Opc.GatewayLib needs open62541, OpenSSL, ZLIB and (non-Debug linux) jsonnet.
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
	set( libDir ${jdeRoot}/libs )
	add_subdirectory( ${libDir}/fwk/src ../../libs/fwk/lib )
	add_subdirectory( ${libDir}/db/src ../../libs/db/lib )
	add_subdirectory( ${libDir}/db/drivers/sqlite/src ../../libs/db/drivers/sqlite/lib )
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
	add_subdirectory( ${jdeRoot}/apps/AppServer/config/sql/sqlite ../../apps/AppServer/config/sql/sqlite )
	add_subdirectory( ${jdeRoot}/apps/AppServer/src ../../apps/AppServer/lib )
	add_subdirectory( ${jdeRoot}/apps/OpcGateway/config/sql/sqlite ../../apps/OpcGateway/config/sql/sqlite )
	add_subdirectory( ${jdeRoot}/apps/OpcGateway/src ../../apps/OpcGateway/lib )
	add_subdirectory( src lib )

	if( jde_TESTS )
		enable_testing()
		add_subdirectory( tests )
	endif()
endif()

set( targetName Jde.Opc.Hub )

add_executable( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp src/main.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )
if( WIN32)
	list( APPEND sources src/OpcHub.rc )
	jdeVersionResource( ${targetName} ) #the .rc's version values from JDE_VERSION (build/functions.cmake)
endif()
target_sources( ${targetName} PRIVATE ${sources} ${resources} ${headers} )

if( WIN32 )
	target_link_libraries( ${targetName} Iphlpapi.lib )
else()
	target_link_libraries( ${targetName} stdc++exp )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} libjsonnet++.so libjsonnet.so )
	endif()
endif()
target_link_libraries( ${targetName} OpenSSL::SSL )
target_link_libraries( ${targetName} Jde.Opc.HubLib )

#Single TU (src/main.cpp): reuse Jde.App.Server's PCH as the gateway exe does (same header set, defines and flags) rather
#than building a third copy.  Guarded: a standalone configure of this app does not define Jde.App.Server.
if( TARGET Jde.App.Server )
	target_precompile_headers( ${targetName} REUSE_FROM Jde.App.Server )
endif()

#No meta of its own: Opc.Hub.jsonnet mounts the AppServer's and the gateway's meta files, each of which imports the
#common-meta.libsonnet symlinked beside it.  Both links here so a standalone configure of the hub resolves them too.
linkConfigFile( "${jdeRoot}/libs/db/config/common-meta.libsonnet" "${jdeRoot}/apps/AppServer/config/common-meta.libsonnet" )
linkConfigFile( "${jdeRoot}/libs/db/config/common-meta.libsonnet" "${jdeRoot}/apps/OpcGateway/config/common-meta.libsonnet" )
if( WIN32 )
	set( appOutDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}" )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-c -tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Hub.jsonnet -include=args/sqlServer"
		RUNTIME_OUTPUT_DIRECTORY "${appOutDir}"
		PDB_OUTPUT_DIRECTORY "${appOutDir}"
	)
	copyCommonDlls()
endif()
