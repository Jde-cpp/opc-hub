#pragma once
#include <jde/opc/pubsub/PubSub.h>

namespace Jde::Opc::Emulator{
	//The emulated PLC's own OPC UA server - what a UA-enabled PLC is.  Holds the same nodeset the OpcServer loads
	//(so its tags mirror the server's) and publishes the contract's fields from those local nodes over PubSub.
	//Headless: `Port` only exists because a UA_Server must bind something; nothing connects to it - and since its
	//access control is anonymous-full (UA_ServerConfig_setMinimal) over a writable nodeset, `bind` keeps that door on
	//loopback rather than on every interface, which is what setMinimal would leave.
	struct PlcServer final : noncopyable{
		//`bind` is the listen host: "127.0.0.1" by default, "" for every interface (a deliberately LAN-visible demo PLC).
		PlcServer( UA_UInt16 port, sv bind, const fs::path& nodeset, PubSub::Config&& contract, SRCE )ε;
		~PlcServer();
		α Iterate()ι->void;//drives the publisher's timers; call from the emulator's loop thread.
		α FindField( sv name )Ι->optional<uint>;//index into Contract().Fields, or none when the contract does not publish it.
		α Write( uint field, double value, UA_StatusCode status=UA_STATUSCODE_GOOD )ε->void;//the local node the writer samples - value and quality together.
		α Contract()Ι->const PubSub::Config&{ return _contract; }
		α Port()Ι->UA_UInt16{ return _port; }
		α Ptr()Ι->UA_Server*{ return _server; }//the tests read its config (serverUrls) and its nodes back; the emulator itself never reaches past Write/Iterate.
	private:
		UA_UInt16 _port;
		PubSub::Config _contract;
		UA_Server* _server{};
		up<PubSub::Writer> _writer;
	};
}
