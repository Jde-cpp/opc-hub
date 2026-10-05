cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
message( Jde.Opc )
project( Jde.Opc VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
find_package( OpenSSL REQUIRED )
find_package( open62541 REQUIRED ) #open62541::open62541 (linked below) carries the include/lib dirs, and its config.h defines UA_ENABLE_ENCRYPTION itself (from UA_ENABLE_ENCRYPTION_OPENSSL) - hand-defining it here only risked disagreeing with how the vendor was actually built.

set( targetName Jde.Opc )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp )
file( GLOB_RECURSE headers CONFIGURE_DEPENDS  *.h )
file( GLOB protos CONFIGURE_DEPENDS proto/*.proto )

target_sources( ${targetName} PRIVATE ${sources} ${headers} ${protos} )

if( WIN32 )
	target_compile_definitions( ${targetName} PRIVATE NOMINMAX )
else()
	target_link_libraries( ${targetName} stdc++exp )
endif()

if( WIN32 )
	target_link_libraries( ${targetName} absl::abseil_dll )#ABSL_CONSUME_DLL for the generated .pb.cc - the raw protobuf .lib below doesn't carry it.
	target_link_libraries( ${targetName} ${CMAKE_INSTALL_PREFIX}/protobuf/lib/protobuf$<IF:$<CONFIG:Debug>,d,>.lib )
	target_link_libraries( ${targetName} ${CMAKE_INSTALL_PREFIX}/protobuf/lib/utf8_range.lib ) #ProtoUtils::Utf8; the plain-path protobuf .lib above carries none of its dependencies.
endif()

target_link_libraries( ${targetName} open62541::open62541 )
target_link_libraries( ${targetName} OpenSSL::SSL )
target_link_libraries( ${targetName} fmt::fmt )
target_link_libraries( ${targetName} Boost::json ) #uatypes headers use jvalue; on Windows this is what makes BOOST_JSON_DECL dllimport (Jde.dll exports Boost.JSON).

target_precompile_headers(${targetName}
  PRIVATE
		"pc.h"
)

#Out-of-source, as libs/app/shared does:  `<jde/opc/proto/X.pb.h>` resolves through the exported root.  ${protoOutDir}
#too, because the gateway's generated Opc.From*.pb.h import Opc.Common.proto and so include its header by bare name.
set( protoOutDir ${jdeGeneratedIncludeDir}/jde/opc/proto )
file( MAKE_DIRECTORY ${protoOutDir} )
protobuf_generate(
	TARGET ${targetName}
	IMPORT_DIRS proto
	PROTOC_OUT_DIR ${protoOutDir}
)
suppressProtoWarnings( "${protos}" ${protoOutDir} )
target_include_directories( ${targetName} PUBLIC ${jdeGeneratedIncludeDir} ${protoOutDir} )
