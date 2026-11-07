#pragma once
//The embedded OpcServer's pumps.NodeSet2.xml, which the history and write suites run against:  pump1 to pump5 under the
//Objects folder, each with a status and a motorRpm, in urn:jde:pumps.
#include <open62541/server.h>
#include <jde/opc/UAException.h>
#include "../../../OpcServer/src/globals.h"
#include <jde/opc/server/UAServer.h>
#ifndef let
#define let const auto
#endif

namespace Jde::Opc::Gateway::Tests{
	constexpr sv PumpsUri{ "urn:jde:pumps" };
	//RpmManual stores a change of 10 rpm, ABSOLUTE_VALUE, and Status1, a Boolean, every change, with no HA Configuration.  Rpm3
	//and Rpm4 store a change of 15 rpm, Rpm3 with no time intervals;  pump3's InstrumentRange is 0-1500 and its EURange 0-3000,
	//pump4 has the EURange alone.  Rpm2 is a historized Double in no group here, which HistTests' days hold nothing of, and
	//Status2 isn't historized.
	constexpr UA_UInt32 Pump1{ 5001 }, Pump2{ 5002 }, Pump3{ 5003 }, Pump4{ 5004 }, PumpManual{ 5005 };
	constexpr UA_UInt32 Status1{ 6011 }, Rpm1{ 6012 }, Status2{ 6021 }, Rpm2{ 6022 }, Status3{ 6031 }, Rpm3{ 6032 }, Rpm4{ 6042 }, RpmManual{ 6054 };
	//pumpManual's toHertz( rpm: Double ) -> ( hertz: Double, text: String ), the pumps demo's method, which CallTests call (#318), bound by the
	//OpcServer after the load (Server::ConfigurePumps), which also gives PumpManual - and no other pump - a WriteMask of DisplayName and
	//Description, the one WriteTests write (#417).
	constexpr UA_UInt32 ToHertz{ 6055 };
	//pumpManual._Test (clients plan, phases 8a and 9):  PumpSampleType, a structure of the server's own - Rpm Double, Running Boolean, Label
	//String, Limits Range, History Double[] - and `sample` (TestSample), the variable of it the OpcServer writes after the load (ConfigurePumps), which
	//StructureTests read as fields, write back and read from the historian (#421).
	constexpr UA_UInt32 TestObject{ 5010 }, SampleType{ 3001 }, SampleEncoding{ 3002 }, TestSample{ 6061 };
	//The pumps' namespace on the embedded OpcServer;  0 without one.
	Ξ PumpsNs()ε->NsIndex{
		const auto ua = Server::FindUAServer();
		if( !ua )
			return 0;
		uint ns{};
		UAε( UA_Server_getNamespaceByName(ua->Ptr(), UA_STRING((char*)PumpsUri.data()), &ns) );
		return (NsIndex)ns;
	}
}