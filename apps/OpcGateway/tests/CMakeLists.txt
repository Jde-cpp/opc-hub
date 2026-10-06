cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.Opc.Tests VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

find_package( open62541 REQUIRED )
if( WIN32 )
	add_definitions( -DZLIB_USE_STATIC_LIBS )
	set( ZLIB_ROOT ${CMAKE_INSTALL_PREFIX}/zlib )
else()
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		link_directories( ${JSONNET_DIR}/lib )
	endif()
endif()
find_package( ZLIB REQUIRED )

set( targetName Jde.Opc.Tests )
add_executable( ${targetName} )
compileOptions( ${targetName} )

file( GLOB sources CONFIGURE_DEPENDS *.cpp */*.cpp )
add_dependencies( ${targetName} Jde.DB.MySql ) #built on every platform; odbc only exists on windows.
if( WIN32 )
	list( APPEND sources OpcTests.rc )
	add_dependencies( ${targetName} Jde.DB.Odbc )
endif()
sqliteProcDependencies( ${targetName} Jde.DB.Sqlite.AppServer Jde.DB.Sqlite.OpcGateway ) #the ctest run below dlopens these via config/args/sqlite
file( GLOB headers CONFIGURE_DEPENDS *.h */*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )

target_include_directories( ${targetName} PRIVATE ${jdeRoot}/include/jde/app/proto )

if( NOT jde_Superbuild )
	if( NOT TARGET Jde.Opc.HistorianLib ) #Jde.Opc.ServerLib links it.
		add_subdirectory( ${jdeRoot}/libs/historian/src ../../../libs/historian/lib )
	endif()
	add_subdirectory( ../../OpcServer/src ../../../apps/OpcServer/lib )
	add_subdirectory( ../../AppServer/src ../../../apps/AppServer/lib )
endif()

find_package( GTest REQUIRED )
target_link_libraries( ${targetName} GTest::gtest )

target_link_libraries( ${targetName} Jde Jde.QL Jde.App.Shared Jde.App.Client Jde.Web.Client Jde.Web.Server Jde.App.ServerLib Jde.Opc Jde.Opc.GatewayLib Jde.Opc.ServerLib )
if( WIN32 )
	target_link_libraries( ${targetName} Iphlpapi.lib )
else()
	target_link_libraries( ${targetName} OpenSSL::SSL )
	if( NOT CMAKE_BUILD_TYPE STREQUAL "Debug" )
		target_link_libraries( ${targetName} libjsonnet++.so libjsonnet.so )
	endif()
endif()

addJdeTest( ${targetName} ${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Tests.jsonnet )

if( WIN32 )
	set( appOutDir "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${targetName}" )
	set_target_properties( ${targetName} PROPERTIES
		VS_DEBUGGER_COMMAND_ARGUMENTS "-tests -settings=${CMAKE_CURRENT_SOURCE_DIR}/config/Opc.Tests.jsonnet"
		RUNTIME_OUTPUT_DIRECTORY "${appOutDir}"
	)
	copyCommonDlls()
	target_compile_definitions( ${targetName} PRIVATE NOMINMAX )
	add_custom_command( TARGET ${targetName} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_INSTALL_PREFIX}/libxml2/bin/libxml2.dll" $<TARGET_FILE_DIR:${targetName}>  COMMENT "libxml2.dll" )
endif()
target_precompile_headers( ${targetName} PRIVATE
	pc.h
)