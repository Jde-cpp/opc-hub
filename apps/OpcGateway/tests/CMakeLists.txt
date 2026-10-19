cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.Tests VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

find_package( open62541 REQUIRED )
findZlibAndJsonnet()

set( targetName Jde.Opc.Tests )
add_executable( ${targetName} )
compileOptions( ${targetName} )

file( GLOB sources CONFIGURE_DEPENDS *.cpp */*.cpp )
add_dependencies( ${targetName} Jde.DB.MySql ) #built on every platform; odbc only exists on windows.
if( WIN32 )
	list( APPEND sources OpcTests.rc )
	jdeVersionResource( ${targetName} ) #the .rc's version values from JDE_VERSION (build/functions.cmake)
	add_dependencies( ${targetName} Jde.DB.Odbc )
endif()
sqliteProcDependencies( ${targetName} Jde.DB.Sqlite.AppServer Jde.DB.Sqlite.OpcGateway ) #the ctest run below dlopens these via config/args/sqlite
file( GLOB headers CONFIGURE_DEPENDS *.h */*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

if( NOT jde_Superbuild )
	if( NOT TARGET Jde.Opc.HistorianLib ) #Jde.Opc.ServerLib links it.
		add_subdirectory( ${jdeRoot}/libs/historian/src ../../../libs/historian/lib )
	endif()
	add_subdirectory( ../../OpcServer/src ../../../apps/OpcServer/lib )
	add_subdirectory( ../../AppServer/src ../../../apps/AppServer/lib )
endif()

find_package( GTest REQUIRED )
target_link_libraries( ${targetName} GTest::gtest )

target_link_libraries( ${targetName} Jde.App.ServerLib Jde.Opc.GatewayLib Jde.Opc.ServerLib )
linkAppRuntime( ${targetName} NOMINMAX LIBXML2 )

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Tests.jsonnet )

if( WIN32 )
	set( appOutDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}" )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Tests.jsonnet"
		RUNTIME_OUTPUT_DIRECTORY "${appOutDir}"
	)
endif()
target_precompile_headers( ${targetName} PRIVATE
	pc.h
)