cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.Server.Tests VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

find_package( open62541 REQUIRED )
findZlibAndJsonnet()

set( targetName Jde.Opc.Server.Tests )
add_executable( ${targetName} )
compileOptions( ${targetName} )

file( GLOB sources CONFIGURE_DEPENDS *.cpp */*.cpp )
if( WIN32 )
	list( APPEND sources Opc.Server.Tests.rc )
endif()
dbDriverDependencies( ${targetName} Jde.DB.Sqlite.AppServer ) #the ctest run below dlopens this via config/args/sqlite
file( GLOB headers CONFIGURE_DEPENDS *.h */*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

if( NOT jde_Superbuild )
	add_subdirectory( ../../AppServer/src ../../../apps/AppServer/lib )
endif()

find_package( GTest REQUIRED )
target_link_libraries( ${targetName} GTest::gtest )
target_link_libraries( ${targetName} Jde.App.ServerLib Jde.Opc.ServerLib )
linkAppRuntime( ${targetName} NOMINMAX LIBXML2 )

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Server.Tests.jsonnet )

set( appOutDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}" )
if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Server.Tests.jsonnet"
		RUNTIME_OUTPUT_DIRECTORY "${appOutDir}"
	)
endif()

target_precompile_headers( ${targetName} PRIVATE pc.h )