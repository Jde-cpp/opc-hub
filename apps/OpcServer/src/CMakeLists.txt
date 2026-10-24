cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.Server VERSION ${JDE_VERSION} LANGUAGES CXX )

include( ${jdeRoot}/build/dependencies.cmake )

find_package( open62541 REQUIRED )
findNodesetLoader()

set( targetName Jde.Opc.ServerLib )
add_library( ${targetName} )
compileOptions( ${targetName} )
file(GLOB sources CONFIGURE_DEPENDS *.cpp */*.cpp )
get_filename_component( main_cpp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ABSOLUTE )
list( REMOVE_ITEM sources ${main_cpp} )
file(GLOB headers CONFIGURE_DEPENDS "*/*.h")
target_sources( "${targetName}" PRIVATE "${sources}" "${headers}" )

target_link_libraries( ${targetName} PUBLIC Jde.App.Client Jde.Web.Server Jde.Opc.UAServer ) #which carries Jde.Opc and the historian.
linkNodesetLoader( ${targetName} PUBLIC )
target_precompile_headers( ${targetName}
	PRIVATE
		pc.h
)