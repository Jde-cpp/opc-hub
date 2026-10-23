cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
message( "Jde.DB" )
set( targetName Jde.DB )
project( ${targetName} VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

if( WIN32 )
	link_directories( ${CMAKE_BINARY_DIR}/../framework/${CMAKE_BUILD_TYPE_INIT} )
endif()
add_library( ${targetName} SHARED )
compileOptions( ${targetName} )
jdeVersionResource( ${targetName} DESCRIPTION "Jde database layer - schema, queries and the driver interface" )

file( GLOB sources CONFIGURE_DEPENDS awaits/*.cpp generators/*.cpp *.cpp meta/*.cpp meta/ddl/*.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h ${jdeRoot}/include/jde/db/*.h ${jdeRoot}/include/jde/db/meta/*.h ${jdeRoot}/include/jde/db/syntax/*.h )
target_sources(${targetName}
    PRIVATE
        ${sources}
        ${headers}
)

target_link_libraries( ${targetName} PRIVATE fmt::fmt )
if( WIN32 ) #PUBLIC: lld-link requires every symbol resolved at link time, and every Jde.DB consumer's translation units instantiate Jde framework templates directly - propagate the requirement instead of repeating it per consumer.
	target_link_libraries( ${targetName} PUBLIC Jde )
endif()

target_precompile_headers(${targetName}
  PUBLIC
  <jde/fwk.h>
)