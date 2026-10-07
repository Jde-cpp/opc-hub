cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
message( "Jde.Opc.GatewayLib" )
project( Jde.Opc.GatewayLib VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

set( targetName Jde.Opc.GatewayLib )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp async/*.cpp auth/*.cpp ql/*.cpp types/*.cpp types/proto/*.cpp uatypes/*.cpp )
file( GLOB protos CONFIGURE_DEPENDS types/proto/*.proto )
get_filename_component( main_cpp ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp ABSOLUTE )
list( REMOVE_ITEM sources ${main_cpp} )
file( GLOB headers CONFIGURE_DEPENDS */*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} ${protos} )

sqliteProcDependencies( ${targetName} Jde.DB.Sqlite.OpcGateway ) #config/args/sqlite + the 'Debug - Sqlite' launch config load these

target_include_directories( ${targetName} PRIVATE ${jdeGeneratedIncludeDir}/jde/app/proto ) #the generated Opc.*.pb.h import Common.proto and include Jde.App.Shared's Common.pb.h by bare name.
target_link_libraries( ${targetName} Jde.Web.Server Jde.App.Client Jde.Opc Jde.Opc.HistorianLib ) #the historian:  today the pass-through's continuation (ql/HistQL.h), the gateway's groups with Phase 5.

target_precompile_headers(${targetName}
	PRIVATE
	pc.h
)

set( PROTO_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR} )
#Out-of-source - see the note in libs/app/shared/CMakeLists.txt.  protoIncludeDir stays in the source tree: it is an
#IMPORT_DIRS entry, i.e. where the hand-written .proto files live, not where generated output goes.
set( protoOutDir ${CMAKE_CURRENT_BINARY_DIR}/types/proto )
file( MAKE_DIRECTORY ${protoOutDir} )#protoc will not create it and Unix Makefiles does not either - see libs/app/shared.
set( protoIncludeDir "${jdeRoot}/libs/app/shared/proto" )
#Stale-output purge which moved Opc.Common.proto to libs/opc:  a build dir configured before it still holds this
#dir's generated copy, and Opc.From*.pb.h's `#include "Opc.Common.pb.h"` finds its own directory first.  Remove after 2026-12-31.
file( REMOVE ${protoOutDir}/Opc.Common.pb.h ${protoOutDir}/Opc.Common.pb.cc )
protobuf_generate(
	TARGET ${targetName}
	IMPORT_DIRS
		"types/proto"
		"${protoIncludeDir}"
		"${jdeRoot}/libs/opc/src/proto"
	PROTOC_OUT_DIR ${protoOutDir})

suppressProtoWarnings( "${protos}" ${protoOutDir} )
#PUBLIC: the tests and soak targets link this lib and include its generated headers by bare name.
target_include_directories( ${targetName} PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${protoOutDir} )
