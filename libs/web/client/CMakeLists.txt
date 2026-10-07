cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Web.Client VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

if( WIN32 )
	set( ZLIB_ROOT ${CMAKE_INSTALL_PREFIX}/zlib )
	find_package( OpenSSL REQUIRED )
endif()
find_package( ZLIB REQUIRED )
include_directories( ${ZLIB_INCLUDE_DIRS} )

set( targetName Jde.Web.Client )
add_library( ${targetName} )
compileOptions( ${targetName} )
set( CMAKE_INCLUDE_CURRENT_DIR ON )
file( GLOB sources CONFIGURE_DEPENDS *.cpp http/*.cpp socket/*.cpp )
set( includeDir ${jdeRoot}/include/jde/web/client )
file( GLOB headers CONFIGURE_DEPENDS *.h ${includeDir}/*.h ${includeDir}/http/*.h ${includeDir}/socket/*.h  )
file( GLOB protos CONFIGURE_DEPENDS proto/*.proto )
target_sources( ${targetName} PRIVATE ${sources} ${headers} ${protos} )

target_link_libraries( ${targetName} PUBLIC ZLIB::ZLIB Boost::json OpenSSL::SSL fmt::fmt )

target_precompile_headers(${targetName}
  PUBLIC
	"pc.h"
)

#Out-of-source - see the note in libs/app/shared/CMakeLists.txt.
set( protoOutDir ${jdeGeneratedIncludeDir}/jde/web/client/proto )
file( MAKE_DIRECTORY ${protoOutDir} )
protobuf_generate(
	TARGET ${targetName}
	IMPORT_DIRS proto
	PROTOC_OUT_DIR ${protoOutDir}
	EXPORT_MACRO Jde_Web_Client_EXPORT
)
suppressProtoWarnings( "${protos}" ${protoOutDir} )
target_include_directories( ${targetName} PUBLIC ${jdeGeneratedIncludeDir} ${protoOutDir} ) #the root for `<jde/web/client/proto/Web.FromServer.pb.h>`, the proto dir for protoc's own bare-name includes.
