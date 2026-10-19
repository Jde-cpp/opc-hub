cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.App.ServerLib )

include( ${jdeRoot}/build/dependencies.cmake )

set( targetName Jde.App.ServerLib )
add_library( ${targetName} )
compileOptions( ${targetName} )
file(GLOB sources CONFIGURE_DEPENDS *.cpp */*.cpp )
get_filename_component( main_cpp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ABSOLUTE )
list( REMOVE_ITEM sources ${main_cpp} )
file( GLOB headers CONFIGURE_DEPENDS *.h */*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

add_dependencies( ${targetName} Jde.DB.MySql ) #built on every platform; odbc only exists on windows.
if( WIN32 )
	add_dependencies( ${targetName} Jde.DB.Odbc )
endif()
sqliteProcDependencies( ${targetName} Jde.DB.Sqlite.AppServer ) #config/args/sqlite + the 'Debug - sqlite' launch config load these

target_link_libraries( ${targetName} Jde.Access Jde.Web.Server Jde.App.Shared )

target_precompile_headers( ${targetName} PRIVATE
	<jde/fwk.h>
	<jde/fwk/io/protobuf.h>
	externals.h
	usings.h
)