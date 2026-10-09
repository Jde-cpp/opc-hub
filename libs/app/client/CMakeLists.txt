cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.App.Client VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

set( targetName Jde.App.Client )
add_library( ${targetName} )
compileOptions( ${targetName} )
file(GLOB sources CONFIGURE_DEPENDS *.cpp awaits/*.cpp )
set( includeDir ${jdeRoot}/include/jde/app/client )
file(GLOB headers CONFIGURE_DEPENDS *.h ${includeDir}/*.h ${includeDir}/awaits/*.h )
target_sources( ${targetName}
    PRIVATE
        "${sources}"
        "${headers}")

target_link_libraries( ${targetName} PUBLIC Jde.App.Shared )
target_precompile_headers(${targetName}
  PRIVATE
	pc.h
)