#pragma once
//The embedded OpcServer's pumps.NodeSet2.xml, which the history and write suites run against:  pump1 to pump5 under the
//Objects folder, each with a status and a motorRpm, in urn:jde:pumps.
#include <open62541/server.h>
#include <jde/opc/UAException.h>
#include "../../../OpcServer/src/globals.h"
#include "../../../OpcServer/src/UAServer.h"
#ifndef let
#define let const auto
#endif

namespace Jde::Opc::Gateway::Tests{
	constexpr sv PumpsUri{ "urn:jde:pumps" };
	//RpmManual stores a change of 10 rpm, ABSOLUTE_VALUE, and Status1, a Boolean, every change, with no HA Configuration.  Rpm3
	//and Rpm4 store a change of 15 rpm, Rpm3 with no time intervals;  pump3's InstrumentRange is 0-1500 and its EURange 0-3000,
	//pump4 has the EURange alone.  Rpm2 is a historized Double in no group here, which HistTests' days hold nothing of, and
	//Status2 isn't historized.
	constexpr UA_UInt32 Pump1{ 5001 }, Pump2{ 5002 }, Pump3{ 5003 }, Pump4{ 5004 };
	constexpr UA_UInt32 Status1{ 6011 }, Rpm1{ 6012 }, Status2{ 6021 }, Rpm2{ 6022 }, Status3{ 6031 }, Rpm3{ 6032 }, Rpm4{ 6042 }, RpmManual{ 6054 };
	//The pumps' namespace on the embedded OpcServer;  0 without one.
	Ξ PumpsNs()ε->NsIndex{
		const auto ua = Server::FindUAServer();
		if( !ua )
			return 0;
		size_t ns{};
		UAε( UA_Server_getNamespaceByName(ua->Ptr(), UA_STRING((char*)PumpsUri.data()), &ns) );
		return (NsIndex)ns;
	}
}