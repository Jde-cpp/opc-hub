cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../build/functions.cmake )
message( Jde.Opc.OpenProtocol )
project( Jde.Opc.OpenProtocol VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

#The Open Protocol codec (docs/open-protocol/spec.md *The codec*):  the header and field codec and a type per MID the
#driver speaks, with no I/O and no open62541, so the fuzz target (../tests/fuzz) compiles these sources as they are.
#Static, so no exports header.
set( targetName Jde.Opc.OpenProtocol )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h ${jdeRoot}/include/jde/opc/openProtocol/*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
target_link_libraries( ${targetName} Jde )

target_precompile_headers( ${targetName} PRIVATE pc.h )