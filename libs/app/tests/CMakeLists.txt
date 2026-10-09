cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )

set( targetName Jde.App.Tests )
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
#CONFIGURE_DEPENDS, as libs/access/tests does and for the reason functions.cmake's header note gives: repo-wide the globs
#omit it on purpose, because a source missing from a stale glob is a link error and that is the check - but a gtest
#executable is the documented exception.  A new self-contained *Tests.cpp is referenced by no other translation unit, so
#a stale glob drops it with no link error, no warning and a green run (app-review3 T9, access-review3 #30).
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
target_compile_definitions( ${targetName} PRIVATE TESTS )
target_link_libraries( ${targetName} GTest::gtest Threads::Threads )
target_link_libraries( ${targetName} Jde.App.Client Jde.Web.Server )

if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/App.Tests.jsonnet" )
	copyCommonDlls()
else()
	target_link_libraries( ${targetName} libssl.a libcrypto.a libjitterentropy.a )
	if( CMAKE_CXX_COMPILER_ID STREQUAL "Clang" )
		target_link_libraries( ${targetName} libzstd.a )
	endif()
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} jsonnet++ libjsonnet.a ) #jsonnet
	endif()
endif()

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/App.Tests.jsonnet )

target_precompile_headers( ${targetName} PRIVATE pc.h )
