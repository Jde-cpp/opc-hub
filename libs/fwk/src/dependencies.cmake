boost()
find_package( OpenSSL REQUIRED )

include_directories( ${jdeRoot}/include )

find_package( fmt REQUIRED )

find_package( spdlog REQUIRED )

find_package( absl CONFIG REQUIRED )
include_directories( ${spdlog_DIR}/../../../include ) #${spdlog_INCLUDE_DIRS}

set( JSONNET_DIR ${CMAKE_INSTALL_PREFIX}/jsonnet CACHE PATH "Jsonnet directory" )
include_directories( ${JSONNET_DIR}/include )
link_directories( ${JSONNET_DIR}/lib )
