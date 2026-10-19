cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )

include( ${CMAKE_CURRENT_LIST_DIR}/../../build/functions.cmake )
project( Jde.App.Server VERSION ${JDE_VERSION} LANGUAGES CXX )
enable_testing()

if( NOT jde_Superbuild )
	add_subdirectory( config/sql/sqlite )
	add_subdirectory( ${jdeRoot}/libs/fwk/src ../../libs/fwk/lib )
	add_subdirectory( ${jdeRoot}/libs/db/src ../../libs/db/lib )
	add_subdirectory( ${jdeRoot}/libs/db/drivers/sqlite/src ../../libs/db/drivers/sqlite/lib ) #the config/sql/sqlite MODULE above is only reachable through this driver
	add_subdirectory( ${jdeRoot}/libs/db/drivers/mysql/src ../../libs/db/drivers/mysql/lib )
	if( WIN32 ) #odbc is windows-only; mysql builds everywhere.
		add_subdirectory( ${jdeRoot}/libs/db/drivers/odbc/src ../../libs/db/drivers/odbc/lib )
	endif()
	add_subdirectory( ${jdeRoot}/libs/ql/src ../../libs/ql/lib )
	add_subdirectory( ${jdeRoot}/libs/access/src ../../libs/access/lib )
	add_subdirectory( ${jdeRoot}/libs/web/server ../../libs/web/server )
	add_subdirectory( ${jdeRoot}/libs/web/client ../../libs/web/client )
	add_subdirectory( ${jdeRoot}/libs/app/shared ../../libs/app/shared )
	add_subdirectory( src lib )
	if( jde_TESTS )
		add_subdirectory( tests )
	endif()
endif()

if( WIN32 )
	add_definitions( -DZLIB_USE_STATIC_LIBS )
	set( ZLIB_ROOT ${CMAKE_INSTALL_PREFIX}/zlib )
else()
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		link_directories( ${JSONNET_DIR}/lib )
	endif()
endif()
find_package( ZLIB REQUIRED )

include( ${jdeRoot}/build/dependencies.cmake )
set( targetName Jde.App.Server )

add_executable( ${targetName} )
compileOptions( ${targetName} )
set( sources src/main.cpp )
if( WIN32 )
	list( APPEND sources src/AppServer.rc )
endif()
target_sources( ${targetName} PRIVATE ${sources} )

if( NOT WIN32 )
	target_link_libraries( ${targetName} OpenSSL::SSL )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} libjsonnet++.so libjsonnet.so )
	endif()
endif()
target_link_libraries( ${targetName} Jde.App.ServerLib )

linkConfigFile( "${jdeRoot}/libs/db/config/common-meta.libsonnet" "${jdeRoot}/apps/AppServer/config/common-meta.libsonnet" )
if( WIN32 )
	set( appOutDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}" )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-c -tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/App.Server.jsonnet"
		RUNTIME_OUTPUT_DIRECTORY "${appOutDir}"
		PDB_OUTPUT_DIRECTORY "${appOutDir}"
	)
	copyCommonDlls()
endif()