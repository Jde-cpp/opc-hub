cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../../build/functions.cmake )
project( Jde.DB.MySql VERSION ${JDE_VERSION} LANGUAGES CXX )
message( ${PROJECT_NAME} )
include( ${jdeRoot}/build/dependencies.cmake )

if( NOT WIN32 ) #windows gets Boost::charconv from boost() in functions.cmake - the source tree there ships no BoostConfig.cmake.
	find_package( Boost REQUIRED COMPONENTS charconv )
endif()
find_package( Threads REQUIRED )

set( targetName Jde.DB.MySql )
add_library( ${targetName} MODULE )
compileOptions( ${targetName} )
jdeVersionResource( ${targetName} DESCRIPTION "Jde MySQL driver" )
target_compile_definitions( ${targetName} PRIVATE JDE_MYSQL_EXPORTS ) #exports.h guards on this, not CMake's auto Jde_DB_MySql_EXPORTS - without it ΓMY is dllimport on windows and GetDataSource never leaves the MODULE.

set_property( TARGET ${targetName} PROPERTY POSITION_INDEPENDENT_CODE ON )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )

# main Contains Boost.MySQL sources via #include <boost/mysql/src.hpp>
target_sources(${targetName} PRIVATE ${sources} ${headers} )
target_link_libraries(${targetName} PRIVATE Boost::charconv Threads::Threads OpenSSL::Crypto OpenSSL::SSL)

# We need to define BOOST_MYSQL_SEPARATE_COMPILATION in any code using Boost.MySQL in separate-build mode
target_compile_definitions(${targetName} PRIVATE BOOST_MYSQL_SEPARATE_COMPILATION)

target_link_libraries( ${targetName} PRIVATE fmt::fmt )
target_link_libraries( ${targetName} PRIVATE Jde.DB ) #PUBLIC-links Jde on WIN32, propagated transitively.

target_precompile_headers( ${targetName}
  PRIVATE
	<boost/mysql.hpp>
	<jde/fwk.h>
	<jde/fwk/str.h>
	<jde/fwk/io/json.h>
	<jde/fwk/chrono.h>
	usings.h
)