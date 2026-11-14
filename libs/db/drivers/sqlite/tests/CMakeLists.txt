cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../../build/functions.cmake )

set( targetName Jde.DB.Sqlite.Tests )
project( ${targetName} VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
message( ${targetName} )

find_package( GTest REQUIRED )
find_package( Threads REQUIRED )
list( APPEND CMAKE_PREFIX_PATH ${CMAKE_INSTALL_PREFIX}/sqlite ) #as ../src does - see its CMakeLists.
find_package( SQLite3 REQUIRED )

if( NOT WIN32 )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		set( jsonnet_DIR ${CMAKE_INSTALL_PREFIX}/jsonnet CACHE PATH "Jsonnet directory" )
		link_directories( ${jsonnet_DIR}/lib )
	endif()
endif()

add_executable( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
#SqliteProcs/SqliteRow are compiled in, not linked: the driver is loaded at runtime, so the registry inside it is
#unreachable from here.  This gives the tests their own instance of it - isolated from the driver's, which is what
#lets ProcRegistryTests register throwaway names without disturbing the real twins.
target_sources( ${targetName} PRIVATE ${sources} ${CMAKE_CURRENT_LIST_DIR}/../src/SqliteProcs.cpp ${CMAKE_CURRENT_LIST_DIR}/../src/SqliteRow.cpp )
target_compile_definitions( ${targetName} PRIVATE TESTS JDE_SQLITE_EXPORTS )
sqliteProcDependencies( ${targetName} Jde.DB.Sqlite.AppServer Jde.DB.Sqlite.OpcGateway )
target_link_libraries( ${targetName} GTest::gtest Threads::Threads SQLite::SQLite3 )
target_link_libraries( ${targetName} Jde.Access Jde.DB Jde )

if( NOT WIN32 )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} jsonnet++ libjsonnet.a ) #jsonnet
	endif()
endif()

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Sqlite.Tests.jsonnet )

testPch( ${targetName} Jde.DB ) #the library it links: Jde.DB.Sqlite compiles -fvisibility=hidden, which its PCH records.

if( WIN32 )
	#copyLibDlls, not copyCommonDlls: this exe links into bin/ alongside Jde/Jde.DB, so staging those would copy them onto
	#themselves.  fmt/zlib still have to be copied in - copyLibDlls is the only thing that puts them there, so without this
	#`--target Jde.DB.Sqlite.Tests` on a clean tree leaves bin without fmtd.dll and Jde.dll fails to load.
	copyLibDlls()
endif()