#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include "Signals.h"

namespace Jde::Opc::Emulator{
	struct Device; struct Emulator;
	struct Tag{
		TagSpec Spec;
		up<IGenerator> Generator;//null for command tags.
		optional<uint> Field;//index into the PubSub contract when published; else written over the client session.
		NodeId Node;//on the OpcServer - command tags and session-written tags, resolved per session.
		double Value{};//the last published reading - held through a quality window that holds.
		TagQuality Quality;
		UA_StatusCode Status{};//Value's quality (OPC 10000-4 7.38); published with the value, and written with it where the server lets a session.
		bool StatusRefused{};//this session's server refused a non-Good status (no StatusWrite) - the value goes alone, see Cycle.
		bool Seen{};//a monitored item's first notification is the current value, not a change.
		Device* Owner{};
		Emulator* Self{};
	};
	struct Device{
		string Path, Name;//Name = the last path segment: "pump1", or "a/b~pump1" -> "pump1"; the contract's field names are "<Name>.<tag>".
		vector<Tag> Tags;
		bool Command{ true };//the last `status` seen - the run command the UI owns.
	};
	//Answers whether the contract publishes "<device>.<tag>" - PlcServer::FindField - and its field index when it does.
	//Empty for the write transport, where nothing is published and every generated tag goes over the session.
	using FindField = function<optional<uint>(sv name)>;
	///emulator/devices -> the devices, generators built, published fields mapped, Owner set.  Self is the caller's:
	//nothing here needs an Emulator, a session or a PLC server, which is what makes it a unit (T2).  Throws on an empty
	//list, a device without tags, or any TagSpec refusal (#9).
	α ParseDevices( const jarray& devices, const FindField& findField, SRCE )ε->vector<Device>;
}
