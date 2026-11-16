#pragma once
#include <jde/opc/uatypes/NodeId.h>
#include "Signals.h"

namespace Jde::Opc::Emulator{
	struct Emulator;
	//One entry of a device's `stops` or `slow` list:  a Recurrence on the device's clock, with the owner's own key.
	struct Stop : Recurrence{
		Stop( const jobject& o, sv device, SRCE )ε;
		UA_Int32 Code;//what `reasonCode` reads through the stop;  0, none given:  a stop with no reason.
	};
	//A performance loss with `status` left on:  the production cycle takes Factor times as long through it.
	struct Slow : Recurrence{
		Slow( const jobject& o, sv device, SRCE )ε;
		double Factor;//over 1.
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
		vector<Stop> Stops;//any active holds `status` off;  the first active with a code gives `reasonCode`.
		vector<Slow> Slows;//the first active gives the factor.
		//A generated `status`'s own generator, which Sample reads before the stops;  the tag itself publishes On.
		up<IGenerator> StatusSource;
		bool On{ true };//`status` as of the last Sample:  the command, or StatusSource where `status` is generated, and off through a stop.
		UA_Int32 Code{};//the first active coded stop's, else 0 - what `reasonCode` publishes.
		//A period:  the stops' clock, `status`, then every generated tag's Value and Status - a command tag has neither.  The period
		//is cut at each stop's and slow period's start and end, so each holds for its length whatever the period;  a counter
		//and a random walk, which step once a period, are sampled once, after the pieces.
		α Sample( Duration dt )ι->void;
		//Each stop and slow period at its End on to the next, from Engine in the lists' order:  a seed draws the same windows whatever
		//the periods.  The tags' quality windows move on in their Apply.
		α Advance()ι->void;
		//Moves Start so that Start+Clock is `wall`, and each {at, for} window with it, the tags' quality windows' too:  their
		//times of day are the wall clock's, which runs on through a pause the steady clock misses (a suspend, a VM paused).  The
		//other shapes keep the device clock.
		α Anchor( TimePoint wall )ι->void;
		optional<uint> StatusTag, ReasonTag;//indexes into Tags:  the bool tag named `status`;  `reasonCode`, which a stops list adds.
		Duration Clock{};
		TimePoint Start;//the wall time of Clock 0, as Anchor last set it, which the {at, for} entries read.
		const std::chrono::time_zone* Zone{};//the emulator's;  null on a device with no {at, for} window, a tag's included.
		//The random stops' and slow periods' draws.  Seeded from `seed`, else from the parse's start time and the device's place in
		//the list, which ParseDevices logs:  a std engine, so a seed draws the same stops in every run, where a seeded absl::BitGen
		//is salted per process.  A tag's random quality windows draw from an engine of its own, seeded from this one's seed and
		//the tag's place in its list (TagQuality), so a tag gaining a window leaves the stops where they were.
		std::mt19937_64 Engine;
	};
	//Answers whether the contract publishes "<device>.<tag>" - PlcServer::FindField - and its field index when it does.
	//Empty for the write transport, where nothing is published and every generated tag goes over the session.
	using FindField = function<optional<uint>(sv name)>;
	///emulator/devices -> the devices, generators built, published fields mapped, Owner set.  Self is the caller's:
	//nothing here needs an Emulator, a session or a PLC server, which is what makes it a unit (T2).  A production tag with
	//a reject share is followed by its reject tag, and a `stops` list adds `reasonCode` as the last.  `zone` is /emulator/zone's,
	//null where it is left out:  UTC, looked up only for a device with an {at, for} window, a stop's, a slow period's or a tag's
	//quality, so a host without the tz database runs the rest.  `start` is the wall time of each device's Clock 0.  Throws on an empty list, a device without tags, any
	//TagSpec refusal (#9), two tags of one name, a `follows` that names no production tag of its device, a stop or slow period
	//of two shapes or with another shape's key, stops on a device whose `status` is not generated, slow periods on one
	//with no production tag, and a command tag beside a generated `status`, which would command nothing.  Owner and the
	//`status` and `reasonCode` generators point into the vector, which never grows after.
	α ParseDevices( const jarray& devices, const FindField& findField, const std::chrono::time_zone* zone, TimePoint start, SRCE )ε->vector<Device>;
}