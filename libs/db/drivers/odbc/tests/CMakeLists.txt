cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../../build/functions.cmake )

set( targetName Jde.DB.Odbc.Tests )
project( ${targetName} VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
message( ${targetName} )

find_package( GTest REQUIRED )
find_package( Threads REQUIRED )

add_executable( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
target_sources( ${targetName} PRIVATE ${sources} )
target_compile_definitions( ${targetName} PRIVATE TESTS )
#No driver link: ConnectTests loads Jde.DB.Odbc.dll from beside the exe, against a DSN that does not exist.
target_link_libraries( ${targetName} GTest::gtest Threads::Threads )
target_link_libraries( ${targetName} Jde.DB Jde )

set_target_properties( ${targetName} PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Odbc.Tests.jsonnet" )
copyCommonDlls() #WIN32-only target: the root CMakeLists adds this directory inside if( WIN32 ).

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Odbc.Tests.jsonnet )

target_precompile_headers( ${targetName} PRIVATE pc.h )