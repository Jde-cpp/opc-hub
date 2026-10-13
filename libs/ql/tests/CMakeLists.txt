cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )

set( targetName Jde.QL.Tests )
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
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
target_compile_definitions( ${targetName} PRIVATE TESTS )
target_link_libraries( ${targetName} GTest::gtest Threads::Threads )
target_link_libraries( ${targetName} Jde.QL Jde.DB Jde ) #no driver linked - the sqlite one is dlopen'd via config/args/sqlite - and no Jde.Access:  the schema is this suite's own (config/qlTests-meta.jsonnet, #238), authorized by nobody.
add_subdirectory( config/sql/sqlite ) #the qlTests schema's insert twins - a MODULE the driver dlopens from the same args.
sqliteProcDependencies( ${targetName} Jde.DB.Sqlite.QLTests ) #the ctest run below dlopens these via config/args/sqlite

if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/QL.Tests.jsonnet -include=args/sqlite -arg path=:memory:" ) #args/sqlite is the only dialect here, so a direct run names it.
	copyCommonDlls()
else()
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} jsonnet++ libjsonnet.a ) #jsonnet
	endif()
endif()

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/QL.Tests.jsonnet )

target_precompile_headers( ${targetName} PRIVATE pc.h )