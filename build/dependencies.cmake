boost()
find_package( OpenSSL REQUIRED )

find_package( fmt REQUIRED ) #no include_directories climb: Jde exports fmt::fmt PUBLIC, leaf targets link fmt::fmt themselves.

find_package( spdlog REQUIRED )
add_compile_definitions( SPDLOG_FMT_EXTERNAL )
include_directories( ${spdlog_DIR}/../../../include )
list( APPEND CMAKE_PREFIX_PATH "${CMAKE_INSTALL_PREFIX}/protobuf/lib/cmake/utf8_range" )

find_package( protobuf CONFIG "35.1.0" EXACT REQUIRED )
find_package( absl CONFIG REQUIRED ) #absl_DIR was previously only set as a side effect of protobuf's find_dependency.
get_filename_component( protobuf_INCLUDE_DIRS ${protobuf_DIR}/../../../include ABSOLUTE )
include_directories( SYSTEM ${protobuf_INCLUDE_DIRS} )
include_directories( SYSTEM ${absl_DIR}/../../../include )
#abseil is built with the presets' cpuFlags and its hash header keys off __SSE4_2__, so a TU compiled without them hashes
#differently from the abseil and protobuf it links - silently.  Fail the configure instead.
if( CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" )
	include( CheckCXXSourceCompiles )
	set( CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY ) #compile only: win-clang cannot link cmake's test exe.
	check_cxx_source_compiles( "#if !defined(__SSE4_2__) || !defined(__PCLMUL__)\n#error\n#endif\nint cpuFlagsCheck();" jdeCpuFlags )
	unset( CMAKE_TRY_COMPILE_TARGET_TYPE )
	if( NOT jdeCpuFlags )
		message( FATAL_ERROR "CMAKE_CXX_FLAGS lacks -msse4.2 -mpclmul, which abseil was built with - configure with a preset (cpuFlags in CMakePresets.common.json)." )
	endif()
endif()
include_directories( ${CMAKE_CURRENT_LIST_DIR}/../include )
