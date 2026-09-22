cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
enable_testing()
include( ${CMAKE_CURRENT_LIST_DIR}/../../build/functions.cmake )
project( Jde.Opc.Server VERSION ${JDE_VERSION} LANGUAGES CXX )

if( NOT jde_Superbuild )
	add_subdirectory( ${jdeRoot}/libs/fwk/src ../../libs/fwk/lib )
	set( opcCommonDir ${jdeRoot}/libs/opc )
	add_subdirectory( ${opcCommonDir}/src ../../libs/opc/lib )
	add_subdirectory( ${jdeRoot}/libs/db/src ../../libs/db/lib )
	add_subdirectory( ${jdeRoot}/libs/db/drivers/sqlite/src ../../libs/db/drivers/sqlite/lib )
	add_subdirectory( ${jdeRoot}/libs/db/drivers/mysql/src ../../libs/db/drivers/mysql/lib )
	if( WIN32 ) #odbc is windows-only; mysql builds everywhere.
		add_subdirectory( ${jdeRoot}/libs/db/drivers/odbc/src ../../libs/db/drivers/odbc/lib )
	endif()
	add_subdirectory( ${jdeRoot}/libs/ql/src ../../libs/ql/lib )
	add_subdirectory( ${jdeRoot}/libs/access/src ../../libs/access/lib )
	add_subdirectory( ${jdeRoot}/libs/web/server ../../libs/web/server )
	add_subdirectory( ${jdeRoot}/libs/web/client ../../libs/web/client )
	add_subdirectory( ${jdeRoot}/libs/app/shared ../../libs/app/shared )
	add_subdirectory( ${jdeRoot}/libs/app/client ../../libs/app/client )
	add_subdirectory( src lib )
	add_subdirectory( emulator )
	if( jde_TESTS )
		add_subdirectory( tests )
	endif()
endif()
find_package( open62541 REQUIRED )
#add_definitions( -DUA_ENABLE_ENCRYPTION_OPENSSL )

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
set( targetName Jde.Opc.Server )

add_executable( ${targetName} )
compileOptions( ${targetName} )

set( sources src/main.cpp )
if( WIN32 )
	list( APPEND sources src/OpcServer.rc )
	jdeVersionResource( ${targetName} ) #the .rc's version values from JDE_VERSION (build/functions.cmake)
endif()
file( GLOB headers CONFIGURE_DEPENDS src/*/*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

if( WIN32 )
	target_link_libraries( ${targetName} Iphlpapi.lib )
else()
	target_link_libraries( ${targetName} OpenSSL::SSL )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} libjsonnet++.so libjsonnet.so )
	endif()
endif()
target_link_libraries( ${targetName} Jde Jde.DB Jde.QL Jde.Access Jde.Web.Server Jde.App.Client Jde.Opc Jde.Opc.ServerLib )

#Single TU (src/main.cpp), but the PUBLIC pc.h of Jde.Web.Client et al. propagates a ~116MB PCH into
#this target - a PCH that costs more to build than the one file it serves.  Jde.App.Server inherits the
#same header set (identical modulo include order) with identical defines and flags, so reuse its PCH
#rather than building a second copy.  Guarded: a standalone (non-superbuild) configure of this app
#does not define Jde.App.Server, and then the target keeps its own PCH.
if( TARGET Jde.App.Server )
	target_precompile_headers( ${targetName} REUSE_FROM Jde.App.Server )
endif()

if( WIN32 )
	set( appOutDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}" )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-c -tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Server.jsonnet"
		RUNTIME_OUTPUT_DIRECTORY "${appOutDir}"
		PDB_OUTPUT_DIRECTORY "${appOutDir}"
	)
	copyCommonDlls()
	add_custom_command( TARGET ${targetName} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_INSTALL_PREFIX}/libxml2/bin/libxml2.dll" $<TARGET_FILE_DIR:${targetName}>  COMMENT "libxml2.dll" )
endif()
