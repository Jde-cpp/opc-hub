cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../../build/functions.cmake )
message( Jde.Opc.OpenProtocol )
project( Jde.Opc.OpenProtocol VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

#Open Protocol (docs/open-protocol/spec.md *The codec*), with no open62541:  the codec, a header and field codec and a
#type per MID the driver speaks, with no I/O, which the fuzz target (../tests/fuzz) compiles as it is;  and over it, on
#asio, the session and the controller's side of one for the suites and the emulator.  Static, so no exports header.
set( targetName Jde.Opc.OpenProtocol )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h ${jdeRoot}/include/jde/opc/openProtocol/*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
target_link_libraries( ${targetName} Jde )

target_precompile_headers( ${targetName} PRIVATE pc.h )