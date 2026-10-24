cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../build/functions.cmake )
message( Jde.Opc )
project( Jde.Opc VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
find_package( open62541 REQUIRED ) #open62541::open62541 (linked below) carries the include/lib dirs, and its config.h defines UA_ENABLE_ENCRYPTION itself (from UA_ENABLE_ENCRYPTION_OPENSSL) - hand-defining it here only risked disagreeing with how the vendor was actually built.

set( targetName Jde.Opc )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp )
file( GLOB_RECURSE headers CONFIGURE_DEPENDS *.h ${jdeRoot}/include/jde/opc/*.h )
list( FILTER headers EXCLUDE REGEX "/include/jde/opc/server/" ) #Jde.Opc.UAServer's
file( GLOB protos CONFIGURE_DEPENDS proto/*.proto )

target_sources( ${targetName} PRIVATE ${sources} ${headers} ${protos} )

if( WIN32 )
	target_compile_definitions( ${targetName} PRIVATE NOMINMAX )
endif()

target_link_libraries( ${targetName} Jde Jde.QL )
#The imported target, not the raw .lib path: protobuf 35's generated code pulls absl (ABSL_CONSUME_DLL on Windows) and
#utf8_range, and only protobuf::libprotobuf carries that chain.
target_link_libraries( ${targetName} protobuf::libprotobuf )
target_link_libraries( ${targetName} open62541::open62541 )
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