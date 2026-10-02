cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
enable_testing()
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Web.Tests VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

if( WIN32 )
	add_definitions( -DZLIB_USE_STATIC_LIBS )
	set( ZLIB_ROOT ${CMAKE_INSTALL_PREFIX}/zlib )
	#OpenSSL is the vcpkg `x64-windows` (dynamic) triplet now - libcrypto-3-x64.dll / libssl-3-x64.dll.  Asking for static
	#here makes vcpkg-cmake-wrapper warn and FindOpenSSL restrict CMAKE_FIND_LIBRARY_SUFFIXES, which finds nothing.
	find_package( OpenSSL REQUIRED )
endif()
find_package( ZLIB REQUIRED )

set( targetName Jde.Web.Tests )
add_executable( ${targetName} )
compileOptions( ${targetName} )


file( GLOB sources CONFIGURE_DEPENDS *.cpp */*.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h */*.h )
file( GLOB protos CONFIGURE_DEPENDS */*.proto )
target_sources( ${targetName} PRIVATE ${sources} ${headers} ${protos} )

if( NOT jde_Superbuild )
	add_subdirectory( ../../fwk/src ../../fwk/lib )
	add_subdirectory( ../../access/src ../../access/lib )
	add_subdirectory( ../../app/shared ../../app/shared )
	add_subdirectory( ../../db/src ../../db/lib )
	add_subdirectory( ../../ql ../../ql )
endif()

set( CMAKE_INCLUDE_CURRENT_DIR ON )

find_package( GTest REQUIRED )
target_link_libraries( ${targetName} GTest::gtest_main )
target_link_libraries( ${targetName} Jde.App.Shared Jde.Web.Server )
target_link_libraries( ${targetName} fmt::fmt )
if( WIN32 )
	target_include_directories( ${targetName} PRIVATE ${ZLIB_INCLUDE_DIRS} )
	target_link_libraries( ${targetName} OpenSSL::SSL )
else()
	target_link_libraries( ${targetName} libssl.a )
	target_link_libraries( ${targetName} libcrypto.a )
	target_link_libraries( ${targetName} libjitterentropy.a )
endif()

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Web.Tests.jsonnet )

if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Web.Tests.jsonnet" )
	copyCommonDlls()
  set_property( DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ${targetName} )
else()
	if( CMAKE_CXX_COMPILER_ID STREQUAL "Clang" )
		target_link_libraries( ${targetName} libzstd.a )
	endif()
endif()

target_precompile_headers( ${targetName} PRIVATE
	pc.h
)

#Out-of-source - see the note in libs/app/shared/CMakeLists.txt.  PROTOC_OUT_DIR is the root, not the proto dir: the
#output keeps the .proto's own relative path, so test.proto lands in ${CMAKE_CURRENT_BINARY_DIR}/proto.
file( MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/proto )
protobuf_generate(
	TARGET ${targetName}
	PROTOC_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR}
)
suppressProtoWarnings( "${protos}" ${CMAKE_CURRENT_BINARY_DIR}/proto )
target_include_directories( ${targetName} PRIVATE ${CMAKE_CURRENT_BINARY_DIR} )