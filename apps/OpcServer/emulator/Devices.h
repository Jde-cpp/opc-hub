#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include "Signals.h"

namespace Jde::Opc::Emulator{
	struct Device; struct Emulator;
	//A scheduled stop:  from each `Every` of the device's clock, which starts with the emulator, for `For`.
	struct Stop{
		Stop( const jobject& o, sv device, SRCE )ε;
		α IsActive( Duration t )Ι->bool{ return t>=Every && t%Every<For; }
		Duration Every, For;
		UA_Int32 Code;//what `reasonCode` reads through the stop;  never 0, which is no reason.
	};
	struct Tag{
		TagSpec Spec;
		up<IGenerator> Generator;//null for command tags.
		optional<uint> Field;//index into the PubSub contract when published; else written over the client session.
		NodeId Node;//on the OpcServer - command tags and session-written tags, resolved per session;  on the PLC's own server under -transport=server, every tag, resolved once.
		//What a generated tag is written as:  under -transport=server the PLC node's, PlcServer::ValueType;  else UInt32 for
		//production's counts and Int32 for the stop code, the pumps' types, which a session write has to match, and null for the rest.
		const UA_DataType* Type{};
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
		vector<Stop> Stops;//the first active wins.
		bool On{ true };//the device's `status` as of the last Sample:  the command, or its `status` tag's own value where that is generated, and off through a stop.
		UA_Int32 Code{};//the active stop's, else 0.
		//One period:  the stops' clock, `status`, then every generated tag's Value and Status - a command tag has neither.
		α Sample( Duration dt )ι->void;
		optional<uint> StatusTag, ReasonTag;//indexes into Tags:  the bool tag named `status`;  `reasonCode`, which a stops list adds.
		Duration Clock{};
	};
	//Answers whether the contract publishes "<device>.<tag>" - PlcServer::FindField - and its field index when it does.
	//Empty for the write transport, where nothing is published and every generated tag goes over the session.
	using FindField = function<optional<uint>(sv name)>;
	///emulator/devices -> the devices, generators built, published fields mapped, Owner set.  Self is the caller's:
	//nothing here needs an Emulator, a session or a PLC server, which is what makes it a unit (T2).  A production tag with
	//a reject share is followed by its reject tag, and a `stops` list adds `reasonCode` as the last.  Throws on an empty
	//list, a device without tags, any TagSpec refusal (#9), or stops on a device whose `status` is not generated.
	α ParseDevices( const jarray& devices, const FindField& findField, SRCE )ε->vector<Device>;
}