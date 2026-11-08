cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
project( Jde.CalendarLib VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )

#The shift calendar (docs/calendar/spec.md):  the model and resolver at the root, the AppServer's awaits under server/ and
#the gateway's cache under client/, one target as Jde.Access is.  Static, so no exports header.  Jde.App.Shared, not the
#socket client or the historian:  the day arithmetic is written here, since Jde.Opc.HistorianLib carries open62541.
set( targetName Jde.CalendarLib )
add_library( ${targetName} )
compileOptions( ${targetName} )

file( GLOB sources CONFIGURE_DEPENDS *.cpp server/*.cpp client/*.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h server/*.h client/*.h
	${jdeRoot}/include/jde/calendar/*.h ${jdeRoot}/include/jde/calendar/server/*.h ${jdeRoot}/include/jde/calendar/client/*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
target_link_libraries( ${targetName} Jde Jde.QL Jde.App.Shared )

target_precompile_headers( ${targetName} PRIVATE pc.h )