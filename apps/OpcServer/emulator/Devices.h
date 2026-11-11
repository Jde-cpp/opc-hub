#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include "Signals.h"

namespace Jde::Opc::Emulator{
	struct Device; struct Emulator;
	//A scheduled stop, {every, for, code}:  from each `every` of the device's clock, which starts with the emulator - the first at
	//`every`, not at 0 - for `for`.  `for` is the schedule's Length, and shorter than `every`.
	struct Stop : Schedule{
		Stop( const jobject& o, sv device, SRCE )ε;
		UA_Int32 Code;//what `reasonCode` reads through the stop;  never 0, which is no reason.
	};
	struct Tag{
		TagSpec Spec;
		up<IGenerator> Generator;//null for command tags.
		optional<uint> Field;//index into the PubSub contract when published; else written over the client session.
		NodeId Node;//on the OpcServer - command tags and session-written tags, resolved per session;  on the PLC's own server under -transport=server, every tag, resolved once.
		//What a generated tag is written as:  its node's DataType where one is read - every tag under -transport=server
		//(PlcServer::ValueType), a session-written one at each connect, a published one the contract's field - else the mode's:
		//Boolean for a toggle, UInt32 for production's counts, Int32 for the stop code and Double for the rest.  Null for command tags.
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
		//The last command seen - the run command the UI owns.  Off on a device with a command tag until its first notification, or
		//under -transport=server its node's value;  through a session outage it holds, as a PLC holds its last command.
		bool Command{ true };
		vector<Stop> Stops;//the first active wins.
		//A generated `status`'s own generator, which Sample reads before the stops;  the tag itself publishes On.
		up<IGenerator> StatusSource;
		bool On{ true };//`status` as of the last Sample:  the command, or StatusSource where `status` is generated, and off through a stop.
		UA_Int32 Code{};//the active stop's, else 0 - what `reasonCode` publishes.
		//A period:  the stops' clock, `status`, then every generated tag's Value and Status - a command tag has neither.  The period
		//is cut at each stop's start and end, so a stop holds the device off for its `For` whatever the period.
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
	//list, a device without tags, any TagSpec refusal (#9), two tags of one name, stops on a device whose `status` is not
	//generated, and a command tag beside a generated `status`, which would command nothing.  Owner and the `status` and
	//`reasonCode` generators point into the vector, which never grows after.
	α ParseDevices( const jarray& devices, const FindField& findField, SRCE )ε->vector<Device>;
}