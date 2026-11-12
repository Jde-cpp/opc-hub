#pragma once

namespace Jde::Opc::Gateway{
	struct ServerCnnctn;
	//What an Open Protocol row's url and `driver` say (docs/open-protocol/spec.md *The row*):  the controller's host and port,
	//the zone its clock keeps and the ceilings on the revisions the driver asks.
	struct OpenProtocolRow final{
		//Refuses, BadRequest:  a url that is not open-protocol://host[:port], the host a name, an address or an IPv6 address in
		//brackets and the port 1 to 65535;  a `driver` that is not a json object, or has a member but `timeZone` and `revisions`;  a
		//`timeZone` that is no name the zone database has;  a `revisions` that is no object of four-digit MIDs to revisions 1 to 999,
		//what the header's three digits hold.
		OpenProtocolRow( const ServerCnnctn& row, SRCE )ε;
		static constexpr sv Scheme{ "open-protocol://" };
		static constexpr uint16_t DefaultPort{ 4545 };//where the url names none (§2.1.1).
		string Host;
		uint16_t Port{ DefaultPort };
		const std::chrono::time_zone* TimeZone{};//null where the row names none:  the gateway's own.
		flat_map<uint16_t,uint16_t> Revisions;//a MID's highest revision to ask, as Session::Settings takes them.
	};
}