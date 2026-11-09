#pragma once
//The embedded OpcServer's pumps.NodeSet2.xml, which the history and write suites run against:  pump1 to pump5 under the
//Objects folder, each with a status and a motorRpm, in urn:jde:pumps.
#include <open62541/server.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/NodeId.h>
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
	//OEE's members on pump1 (docs/oee/spec.md *Emulator*):  totalCount and rejectCount, UInt32 counters, and reasonCode, an Int32,
	//historized as status and motorRpm are;  what OeeTests write as a machine's Total, Reject and Reason signals.
	constexpr UA_UInt32 Total1{ 6013 }, Reject1{ 6014 }, Reason1{ 6015 };
	//pumpManual's toHertz( rpm: Double ) -> ( hertz: Double, text: String ), the pumps demo's method, which CallTests call (#318), bound by the
	//OpcServer after the load (Server::ConfigurePumps), which also gives PumpManual - and no other pump - a WriteMask of DisplayName and
	//Description, the one WriteTests write (#417).
	constexpr UA_UInt32 ToHertz{ 6055 };
	//pumpManual.raiseEvent( message: String, severity: UInt16 ) -> ( eventId: ByteString ), the pumps demo's event source (clients spec
	//*Events*, #424):  a BaseEventType event from pumpManual, whose EventNotifier the nodeset gives the SubscribeToEvents bit, with the
	//pump's motorRpm as its `Rpm` field;  what SubscribeTests' event tests raise.
	constexpr UA_UInt32 RaiseEvent{ 6058 };
	//pumpManual.overspeed (clients spec *Alarms and conditions*, #425), the AlarmConditionType object the server library drives, with its
	//`Raise( active: Boolean, severity: UInt16, message: String ) -> ( eventId: ByteString )`, a transition by hand, and its `HighLimit`,
	//which the pump's motorRpm is compared with:  what AlarmTests raise, refresh, acknowledge and confirm through the gateway (#426).
	constexpr UA_UInt32 Overspeed{ 5020 }, OverspeedRaise{ 6110 }, OverspeedHighLimit{ 6106 };
	//pumpManual._Test (clients plan, phases 8a and 9):  PumpSampleType, a structure of the server's own - Rpm Double, Running Boolean, Label
	//String, Limits Range, History Double[] - and `sample` (TestSample), the variable of it the OpcServer writes after the load (ConfigurePumps), which
	//StructureTests read as fields, write back and read from the historian (#421).
	constexpr UA_UInt32 TestObject{ 5010 }, SampleType{ 3001 }, SampleEncoding{ 3002 }, TestSample{ 6061 };
	//_Test's writable variable of each built-in type, Boolean (i=1) to LocalizedText (i=21), at 6061+<type id> - `TestVariable( UA_NS0ID_GUID )` -
	//and `int32Array`, an Int32[] of any length:  what WriteTests write each type, an array and an index range to (#423).
	constexpr UA_UInt32 TestArray{ 6083 };
	constexpr UA_UInt32 TestVariable( UA_UInt32 builtInTypeId )ι{ return 6061+builtInTypeId; }
	//The pumps' namespace on the embedded OpcServer;  0 without one.
	Ξ PumpsNs()ε->NsIndex{
		const auto ua = Server::FindUAServer();
		if( !ua )
			return 0;
		uint ns{};
		UAε( UA_Server_getNamespaceByName(ua->Ptr(), UA_STRING((char*)PumpsUri.data()), &ns) );
		return (NsIndex)ns;
	}
	//PumpSampleType as the embedded server built it from the nodeset.
	Ξ SampleDataType()ε->const UA_DataType&{
		const NodeId id{ PumpsNs(), SampleType };
		const auto type = UA_Server_findDataType( Server::GetUAServer().Ptr(), &id );
		THROW_IF( !type, "PumpSampleType ({}) is not among the server's types.", id.ToString() );
		return *type;
	}
	//The sample as Value::ToJson writes it and Value::Set reads it back:  `rpm` and `label` the test's, the rest fixed.
	Ξ SampleJson( double rpm, sv label, bool running=true )ι->jobject{
		return { {"Rpm", rpm}, {"Running", running}, {"Label", label}, {"Limits", jobject{{"Low", 0.0}, {"High", 3000.0}}}, {"History", jarray{1470.0, 1475.0, rpm}} };
	}
}