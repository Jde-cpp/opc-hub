cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
enable_testing()
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.App.Server.Tests VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

findZlibAndJsonnet()
find_package( GTest REQUIRED )

set( targetName Jde.App.Server.Tests )
add_executable( ${targetName} )
compileOptions( ${targetName} )

file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB_RECURSE headers CONFIGURE_DEPENDS *.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

dbDriverDependencies( ${targetName} Jde.DB.Sqlite.AppServer ) #the ctest run below dlopens these via config/args/sqlite

if( NOT jde_Superbuild ) #superbuild already added apps/AppServer/src at the root.
	add_subdirectory( ../src ../../../apps/AppServer/lib )
endif()

target_link_libraries( ${targetName} GTest::gtest )
target_link_libraries( ${targetName} Jde.App.ServerLib )
linkAppRuntime( ${targetName} )

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/App.Server.Tests.jsonnet )

if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/App.Server.Tests.jsonnet"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}"
	)
endif()

target_precompile_headers( ${targetName} PRIVATE pc.h )