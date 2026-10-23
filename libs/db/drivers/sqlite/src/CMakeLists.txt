cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../../build/functions.cmake )
project( Jde.DB.Sqlite VERSION ${JDE_VERSION} LANGUAGES CXX )
message( ${PROJECT_NAME} )
include( ${jdeRoot}/build/dependencies.cmake )

list( APPEND CMAKE_PREFIX_PATH ${CMAKE_INSTALL_PREFIX}/sqlite ) #static lib built from the sqlite amalgamation into this prefix; system libsqlite3-dev also works.
find_package( SQLite3 REQUIRED )
find_package( Threads REQUIRED )

set( targetName Jde.DB.Sqlite )
add_library( ${targetName} MODULE ) #dlopen'd only - nothing links it; proc modules reach it through IProcs (sqlite_api.h).
compileOptions( ${targetName} )
jdeVersionResource( ${targetName} DESCRIPTION "Jde SQLite driver" )
target_compile_definitions( ${targetName} PRIVATE JDE_SQLITE_EXPORTS )

set_property( TARGET ${targetName} PROPERTY POSITION_INDEPENDENT_CODE ON )

#Hide everything by default; only GetDataSource (marked ΓLITE=visibility("default")) stays exported for dlsym.
set_target_properties( ${targetName} PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON )
if( NOT WIN32 )
	target_link_options( ${targetName} PRIVATE LINKER:--exclude-libs,ALL ) #don't re-export the statically-linked sqlite3 amalgamation.
endif()
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

target_link_libraries( ${targetName} PRIVATE SQLite::SQLite3 Threads::Threads )
target_link_libraries( ${targetName} PRIVATE fmt::fmt )
target_link_libraries( ${targetName} PRIVATE Jde.DB ) #PUBLIC-links Jde on WIN32, propagated transitively.

target_precompile_headers( ${targetName}
  PRIVATE
	<sqlite3.h>
	<jde/fwk.h>
	<jde/fwk/str.h>
	<jde/fwk/io/json.h>
	<jde/fwk/chrono.h>
	usings.h
)
