cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
message( Jde.Opc.HistorianLib )
project( Jde.Opc.HistorianLib VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
find_package( open62541 REQUIRED )

#Linked into Jde.Opc.ServerLib and Jde.Opc.GatewayLib, the two hosts (spec *Placement*).
set( targetName Jde.Opc.HistorianLib )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB_RECURSE sources CONFIGURE_DEPENDS *.cpp )
file( GLOB_RECURSE headers CONFIGURE_DEPENDS *.h ${jdeRoot}/include/jde/historian/*.h )
file( GLOB protos CONFIGURE_DEPENDS proto/*.proto )
target_sources( ${targetName} PRIVATE ${sources} ${headers} ${protos} )
if( WIN32 )
	target_compile_definitions( ${targetName} PRIVATE NOMINMAX )
endif()

target_link_libraries( ${targetName} Jde.Opc open62541::open62541 fmt::fmt protobuf::libprotobuf )
target_precompile_headers( ${targetName} PRIVATE pc.h )

#Out-of-source, as libs/opc does.  The generated Hist.Records.pb.h includes Opc.Common.pb.h by bare name, which Jde.Opc's
#PUBLIC include of its own proto dir resolves.
set( protoOutDir ${jdeGeneratedIncludeDir}/jde/historian/proto )
file( MAKE_DIRECTORY ${protoOutDir} )
protobuf_generate(
	TARGET ${targetName}
	IMPORT_DIRS proto ${jdeRoot}/libs/opc/src/proto
	PROTOC_OUT_DIR ${protoOutDir}
)
suppressProtoWarnings( "${protos}" ${protoOutDir} )
target_include_directories( ${targetName} PUBLIC ${jdeGeneratedIncludeDir} )
