cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
enable_testing()
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Framework.Tests VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
message( Framework.Tests )
find_package( GTest REQUIRED )

set( targetName Jde.Fwk.Tests )
add_executable( ${targetName} )
compileOptions( ${targetName} )

file( GLOB sources CONFIGURE_DEPENDS *.cpp co/*.cpp io/*.cpp log/*.cpp crypto/*.cpp )
file( GLOB_RECURSE headers CONFIGURE_DEPENDS *.h log/*.h )
file( GLOB protos CONFIGURE_DEPENDS proto/*.proto )
if( WIN32 )
	list( APPEND sources Tests.Framework.rc )
endif()
target_sources( ${targetName} PRIVATE ${sources} ${headers} ${protos} )

target_link_libraries( ${targetName} GTest::gtest GTest::gtest_main )
target_link_libraries( ${targetName} Jde )
if( WIN32 )
	include_directories( ${OPENSSL_ROOT_DIR}/include )
endif()

#The record layer's test record (io/RecordsTests.cpp) embeds the layer's proto, which Jde generates and exports.
set( protoOutDir ${jdeGeneratedIncludeDir}/jde/fwk/tests/proto )
file( MAKE_DIRECTORY ${protoOutDir} )
protobuf_generate(
	TARGET ${targetName}
	IMPORT_DIRS proto ${jdeRoot}/libs/fwk/src/io/records/proto
	PROTOC_OUT_DIR ${protoOutDir}
)
suppressProtoWarnings( "${protos}" ${protoOutDir} )
target_include_directories( ${targetName} PRIVATE ${jdeGeneratedIncludeDir} ${protoOutDir} )

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Framework.Tests.jsonnet )

target_precompile_headers( ${targetName} PRIVATE pc.h )
if( WIN32 )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Framework.Tests.jsonnet"
		RUNTIME_OUTPUT_DIRECTORY "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}"
	)
	copyCommonDlls( Jde ) #no Jde.DB: nothing here links or loads it, and staging it would make `--target ${targetName}` build it.
endif()