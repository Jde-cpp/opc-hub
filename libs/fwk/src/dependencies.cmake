boost()
find_package( OpenSSL REQUIRED )

include_directories( ${jdeRoot}/include )

find_package( fmt REQUIRED )

find_package( spdlog REQUIRED )

find_package( absl CONFIG REQUIRED )
#The one shared protobuf (build/CMakeLists.txt):  Jde links it for the record layer's proto under io/records, and every
#consumer reaches that copy through Jde.  utf8_range is protobuf's config's find_dependency, installed beside it.
list( APPEND CMAKE_PREFIX_PATH "${CMAKE_INSTALL_PREFIX}/protobuf/lib/cmake/utf8_range" )
find_package( protobuf CONFIG "35.1.0" EXACT REQUIRED )
include_directories( ${spdlog_DIR}/../../../include ) #${spdlog_INCLUDE_DIRS}

set( JSONNET_DIR ${CMAKE_INSTALL_PREFIX}/jsonnet CACHE PATH "Jsonnet directory" )
include_directories( ${JSONNET_DIR}/include )
link_directories( ${JSONNET_DIR}/lib )
