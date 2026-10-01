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

target_sources( ${targetName} PRIVATE ${sources} ${headers} )

target_link_libraries( ${targetName} PUBLIC fmt::fmt )
target_link_libraries( ${targetName} PUBLIC Boost::json )
#io/crc.h's Calc32c is inline, so its caller makes the absl call.  PUBLIC also hands every target abseil_dll's
#ABSL_CONSUME_DLL - the ones that only reach absl through protobuf's headers included.
target_link_libraries( ${targetName} PUBLIC absl::crc32c )
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
