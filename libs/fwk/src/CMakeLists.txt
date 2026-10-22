cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Framework VERSION ${JDE_VERSION} LANGUAGES CXX )
message( "${PROJECT_NAME}:  ${PROJECT_VERSION}" )
include( dependencies.cmake )

if( WIN32 )
	find_package( OpenSSL REQUIRED )
	find_package( ryml REQUIRED )
endif()

set( targetName Jde )
add_library( ${targetName} SHARED )
compileOptions( ${targetName} )
jdeVersionResource( ${targetName} DESCRIPTION "Jde framework - logging, settings, I/O and coroutines" )
set_property( TARGET ${targetName} PROPERTY POSITION_INDEPENDENT_CODE ON )
target_compile_definitions( ${targetName} PUBLIC SPDLOG_FMT_EXTERNAL )
#if( WIN32 )
#	target_link_directories( ${targetName} PRIVATE $ENV{REPO_DIR}/jsonnet/vs2017/x64/${CMAKE_BUILD_TYPE} )
#endif()

file(GLOB sources CONFIGURE_DEPENDS *.cpp */*.cpp */*/*.cpp )
file(GLOB headers CONFIGURE_DEPENDS "*/*.h")

set( osDir ${CMAKE_CURRENT_SOURCE_DIR}/process/os )
if( WIN32 )
	set( osDir ${osDir}/windows )
	list( APPEND sources ${osDir}/WindowsApp.cpp ${osDir}/WindowsDrive.cpp ${osDir}/WindowsSvc.cpp  ${osDir}/WindowsThread.cpp ${osDir}/WindowsUtilities.cpp  ${osDir}/WindowsWorker.cpp ${osDir}/WinFileAwait.cpp )
else()
	set( osDir ${osDir}/linux )
  list( APPEND sources ${osDir}/LinuxApp.cpp ${osDir}/LinuxDrive.cpp ${osDir}/LinuxFileAwait.cpp ${osDir}/LinuxThread.cpp )
	find_package( PkgConfig REQUIRED )
	pkg_check_modules( uring REQUIRED IMPORTED_TARGET liburing )
endif()
get_filename_component(thread_cpp ${CMAKE_CURRENT_SOURCE_DIR}/process/thread.cpp ABSOLUTE)
list(REMOVE_ITEM sources "${thread_cpp}")

file( GLOB protos CONFIGURE_DEPENDS io/records/proto/*.proto )
target_sources( ${targetName} PRIVATE ${sources} ${headers} ${protos} )

target_link_libraries( ${targetName} PUBLIC fmt::fmt )
target_link_libraries( ${targetName} PUBLIC Boost::json )
#PUBLIC:  the public headers use absl::Mutex (usings.h), AnyInvocable (execution.h) and Notification (co/Await.h), and
#io/crc.h's inline Calc32c calls absl.  It also hands every target abseil_dll's ABSL_CONSUME_DLL - the ones that only reach
#absl through protobuf's headers included.
target_link_libraries( ${targetName} PUBLIC absl::abseil_dll )
#PUBLIC too:  the record layer's headers (io/records) include its generated proto, and the imported target carries
#PROTOBUF_USE_DLLS and the absl/utf8_range chain protobuf 35's generated code needs, as libs/opc links it.
target_link_libraries( ${targetName} PUBLIC protobuf::libprotobuf )
#Out-of-source, as libs/opc does:  <jde/fwk/proto/X.pb.h> resolves through the exported root, and ${protoOutDir} too, so a
#proto importing Jde.IO.Records.proto finds its header by bare name.
set( protoOutDir ${jdeGeneratedIncludeDir}/jde/fwk/proto )
file( MAKE_DIRECTORY ${protoOutDir} )
#Jde.dll is the one shared library holding generated protos:  protoc marks their classes and globals with JDE_PROTO_EXPORT,
#exported here and imported by every consumer.  Linux exports everything already.
if( WIN32 )
	target_compile_definitions( ${targetName} PRIVATE "JDE_PROTO_EXPORT=__declspec(dllexport)" INTERFACE "JDE_PROTO_EXPORT=__declspec(dllimport)" )
else()
	target_compile_definitions( ${targetName} PUBLIC JDE_PROTO_EXPORT= )
endif()
protobuf_generate(
	TARGET ${targetName}
	IMPORT_DIRS io/records/proto
	PROTOC_OUT_DIR ${protoOutDir}
	EXPORT_MACRO JDE_PROTO_EXPORT
)
suppressProtoWarnings( "${protos}" ${protoOutDir} )
target_include_directories( ${targetName} PUBLIC ${jdeGeneratedIncludeDir} ${protoOutDir} )
if( WIN32 )
	#Jde.dll is the Boost.JSON dll (see boost() in functions.cmake): io/json.cpp compiles <boost/json/src.hpp> in, so
	#BOOST_JSON_DECL must be dllexport throughout this target.  Target-wide rather than in json.cpp alone because the PCH
	#sees the declarations first - baked as dllimport, json.cpp's definitions would be definitions of dllimport functions.
	target_compile_definitions( ${targetName} PRIVATE BOOST_JSON_SOURCE )
endif()

if( WIN32 )
	target_link_libraries( ${targetName} PRIVATE version )
	target_link_libraries( ${targetName} PRIVATE jsonnet jsonnet++ ryml::ryml )
else()
	target_link_libraries( ${targetName} PRIVATE libjsonnet++.so )
	target_link_libraries( ${targetName} PRIVATE PkgConfig::uring )
#	target_link_libraries( ${targetName} PRIVATE libssl.a libcrypto.a )
endif()
target_link_libraries( ${targetName} PUBLIC OpenSSL::SSL )
if( WIN32 )
	#TrustStore.cpp calls CertOpenSystemStoreW/CertEnumCertificatesInStore/CertCloseStore directly (the OpenSSL
	#winstore loader cannot enumerate - it only searches by subject).  crypt32 arrived transitively from the
	#*static* libcrypto.lib; the dynamic build's import lib does not drag it, so name it here.
	target_link_libraries( ${targetName} PUBLIC crypt32 )
endif()

target_precompile_headers( ${targetName}
  PUBLIC
  <jde/fwk.h>
)
