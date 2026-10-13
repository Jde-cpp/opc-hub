cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../../../build/functions.cmake )
project( Jde.DB.Sqlite.QLTests VERSION ${JDE_VERSION} LANGUAGES CXX )
message( ${PROJECT_NAME} )
include( ${jdeRoot}/build/dependencies.cmake )

sqliteProcModule( ${PROJECT_NAME} ) #the qlTests schema's insert twins (../../qlTests-meta.jsonnet) - dlopen'd by the driver from config/args/sqlite, never linked.