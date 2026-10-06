cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.Server VERSION ${JDE_VERSION} LANGUAGES CXX )

include( ${jdeRoot}/build/dependencies.cmake )

find_package( open62541 REQUIRED )
find_package( NodesetLoader REQUIRED )
if( WIN32 )
	if( NOT jde_REPOS )
		set( LIBXML2_LIBRARY ${CMAKE_INSTALL_PREFIX}/libxml2/lib/libxml2.lib CACHE FILEPATH "LibXml2 library" )
		set( LIBXML2_INCLUDE_DIR ${CMAKE_INSTALL_PREFIX}/libxml2/include/libxml2 CACHE PATH "LibXml2 headers" )
	endif()
endif()
find_package( LibXml2 REQUIRED )
set( XML2LIB LibXml2::LibXml2 )

#NodesetLoader_DIR=/home/duffyj/code/libs/install/g++-14/Debug/NodesetLoader/lib/cmake/NodesetLoader
#dumpVariables()

set( targetName Jde.Opc.ServerLib )
add_library( ${targetName} )
compileOptions( ${targetName} )
file(GLOB sources CONFIGURE_DEPENDS *.cpp */*.cpp )
get_filename_component( main_cpp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ABSOLUTE )
list( REMOVE_ITEM sources ${main_cpp} )
file(GLOB headers CONFIGURE_DEPENDS "*/*.h")
target_sources( "${targetName}" PRIVATE "${sources}" "${headers}" )

if( NOT WIN32 )
  target_include_directories( ${targetName} PRIVATE /usr/include/libxml2 )
endif()


target_link_libraries( ${targetName} PUBLIC Jde.App.Client Jde.Web.Server Jde.Opc Jde.Opc.HistorianLib )
if( NOT WIN32 )
	target_link_libraries( ${targetName} PUBLIC protobuf::libprotobuf ) #why not Jde.Web.Server?
endif()
target_link_libraries( ${targetName} PUBLIC ${XML2LIB} )
target_link_libraries( ${targetName} PUBLIC NodesetLoader )
#######################################################
target_precompile_headers( ${targetName}
	PRIVATE
		pc.h
)
