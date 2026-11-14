#pragma once
#include <open62541/server.h>
#include <absl/functional/any_invocable.h>
#include <jde/opc/server/UASettings.h>

namespace Jde::Opc::Gateway{
	struct DriverCalls; struct ServerCnnctn;
	//What sits behind a driver row's UA server (docs/open-protocol/spec.md *The driver server*):  one per kind, made for a row,
	//which builds the address space in the row's namespace, feeds its values and answers its methods.  Its host (DriverHost)
	//knows no kind:  it makes the server, calls these in order, and keeps the server until `stopped` has run and the driver
	//until the server has gone, so a method's callback always finds its driver.
	struct IDriver{
		virtual ~IDriver()=default;
		β Configure( Opc::Server::UASettings& /*settings*/ )ε->void{}//what the kind sets of its server's settings, its limits and how long a call holds, before the server is made.
		//The address space, every node in namespace `ns`, the row's own, before the server runs.  A method that answers later
		//holds its call with `calls`, the host's (DriverCalls.h), and needs no right checked:  the Call service has asked the
		//host for Execute before the callback runs.
		β Build( UA_Server& server, UA_UInt16 ns, sp<DriverCalls> calls )ε->void=0;
		β Start()ι->void=0;//once the server listens.
		//Ends what it does, its link closed as its protocol closes one.  `stopped` runs once, on any thread, when nothing of
		//the driver's will touch the server again, a held call's answer included.
		β Stop( absl::AnyInvocable<void()> stopped )ι->void=0;
	};
	//A kind registers a driver by its entry in drivers/DriverHost.cpp's table:  what makes the kind's driver for a row, the
	//row's own refusal thrown, or null for a kind with none - ua, and one that keeps a row for no server of the gateway's.
	using MakeDriver = up<IDriver>(*)( const ServerCnnctn& row, SL sl );
	α FindDriver( sv kind )ι->MakeDriver;
}