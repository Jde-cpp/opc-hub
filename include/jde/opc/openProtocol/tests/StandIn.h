#pragma once
#include <jde/opc/openProtocol/Controller.h>
#ifndef let
#define let const auto
#endif

//The bench controller's stand-in (docs/open-protocol/plan.md *Phase 10*, 10b):  a Power Focus 6000 with two virtual
//stations as its appendix has one, for the bench's checks to be checked against before the controller is there.  Two of
//the library's test controllers on loopback, each a station that numbers its messages and has the revisions the
//appendix's Table 1 lists, under one run of tightening ids that is not consecutive:  a rundown at one is pushed to that
//station's clients and kept at the other under the station that made it, which is how MID 0064's walk meets it (§2.4).
namespace Jde::Opc::OpenProtocol::Tests{
	struct StandIn final{
		static constexpr array<uint32_t,2> StationIds{ 6534, 6685 };//the appendix's own example's.
		//`ports` 0 takes free ones.
		StandIn( boost::asio::io_context& context, array<uint16_t,2> ports={} )ε:
			_stations{{ {context, ports[0], Pf6000(0)}, {context, ports[1], Pf6000(1)} }}
		{}
		//A virtual station's script:  the parameter set selection at revision 1 and the job info at 4, where the codec has 3
		//and 6, and everything else the codec asks at its own last revision or past it.
		Ω Pf6000( uint station )ι->Controller::Script{
			return {
				.Identity={ .CellId=0, .ChannelId=1, .ControllerName="Bench PF6000", .SupplierCode="ACT", .OpenProtocolVersion="2.8", .ControllerSoftwareVersion="3.10.2",
					.ToolSoftwareVersion="2.1.0", .RbuType="", .ControllerSerialNumber="A6000123", .SystemType=3, .SystemSubtype=1, .SequenceNumberSupport=1, .LinkingSupport=0,
					.StationId=StationIds[station], .StationName=StationName( station ), .ClientId=1 },
				.Revisions={ {Mid0014::Id, 1}, {Mid0034::Id, 4} }
			};
		}
		Ω StationName( uint station )ι->string{ return Ƒ( "Station {}", station+1 ); }
		α Station( uint station )ι->Controller&{ return _stations[station]; }
		α Port( uint station )Ι->uint16_t{ return _stations[station].Port(); }
		//A rundown at `station` under the run's next id, which is its return.
		α Tighten( uint station, Mid0061 result )ι->uint32_t{
			let id = _nextId.fetch_add( 3 );
			result.TighteningId = id;
			_stations[1-station].Keep( result, StationIds[station], StationName(station) );
			_stations[station].Push( move(result) );
			return id;
		}
	private:
		array<Controller,2> _stations;
		atomic<uint32_t> _nextId{ 10204 };
	};
}