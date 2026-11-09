cmake_minimum_required( VERSION ${MIN_REQ_CMAKE_VERSION} )
include( ${CMAKE_CURRENT_LIST_DIR}/../../../build/functions.cmake )
message( Jde.Opc.OeeLib )
project( Jde.Opc.OeeLib VERSION ${JDE_VERSION} LANGUAGES CXX )
include( ${jdeRoot}/build/dependencies.cmake )
find_package( open62541 REQUIRED )

#OEE (docs/oee/spec.md *Placement*):  the engine over a historian group's records, the formulas over a calendar snapshot
#and the rules, with no gateway and no database.  Jde.Opc.GatewayLib is to host it.  Static, as the historian is, so no
#exports header.
set( targetName Jde.Opc.OeeLib )
add_library( ${targetName} )
compileOptions( ${targetName} )
file( GLOB sources CONFIGURE_DEPENDS *.cpp )
file( GLOB headers CONFIGURE_DEPENDS *.h ${jdeRoot}/include/jde/oee/*.h )
target_sources( ${targetName} PRIVATE ${sources} ${headers} )
if( WIN32 )
	target_compile_definitions( ${targetName} PRIVATE NOMINMAX )
endif()

#Jde.Opc.HistorianLib for Hist::Proto::HistoryRecord, ReadValue and IClock;  Jde.CalendarLib for the Schedule.
target_link_libraries( ${targetName} Jde.Opc.HistorianLib Jde.CalendarLib )
target_precompile_headers( ${targetName} PRIVATE pc.h )