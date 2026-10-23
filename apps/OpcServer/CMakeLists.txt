cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
enable_testing()
include( ${CMAKE_CURRENT_LIST_DIR}/../../build/functions.cmake )
project( Jde.Opc.Server VERSION ${JDE_VERSION} LANGUAGES CXX )

if( NOT jde_Superbuild )
	add_subdirectory( ${jdeRoot}/libs/fwk/src ../../libs/fwk/lib )
	set( opcCommonDir ${jdeRoot}/libs/opc )
	add_subdirectory( ${opcCommonDir}/shared/src ../../libs/opc/shared/lib )
	add_subdirectory( ${jdeRoot}/libs/historian/src ../../libs/historian/lib )
	add_subdirectory( ${opcCommonDir}/server/src ../../libs/opc/server/lib )
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

findZlibAndJsonnet()

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

if( NOT WIN32 )
	target_link_libraries( ${targetName} OpenSSL::SSL )
endif()
target_link_libraries( ${targetName} Jde.Opc.ServerLib ) #which carries the app, web, opc, UA server and historian libs, and the nodeset loader.
linkAppRuntime( ${targetName} LIBXML2 ) #no NOMINMAX: the PCH reused below is Jde.App.Server's, built without it.

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
endif()