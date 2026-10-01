cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../../build/functions.cmake )
set( targetName Jde.DB.Odbc )
project( ${targetName} VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
add_library( ${targetName} MODULE )
compileOptions( ${targetName} )

file( GLOB sources CONFIGURE_DEPENDS *.cpp MsSql/*.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )

target_sources( ${targetName} PRIVATE ${sources} ${headers} )

if( WIN32 )
	target_link_libraries( ${targetName} odbc32.dll )
	target_link_libraries( ${targetName} Jde Jde.DB )
	target_compile_definitions( ${targetName} PRIVATE NOMINMAX )
endif()

target_precompile_headers(${targetName}
	PRIVATE
	"pc.h"
)