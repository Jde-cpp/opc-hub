#pragma once
#include <random>
#include <jde/opc/uatypes/NodeId.h>
#include "Signals.h"

namespace Jde::Opc::Emulator{
	struct Device; struct Emulator;
	//One entry of a device's `stops` or `slow` list:  its window on the device's clock, the one it is in or the next, in one of
	//three shapes.  {every, for}:  from each `every` of the clock, which starts with the emulator - the first at `every`, not at
	//0 - for `for`, the shorter.  {mean, min, max}:  after a gap from the last one's end, the first's from 0, drawn about `mean`,
	//exponential, for a length drawn from [min, max], both from the device's engine.  {at, for}:  from a time of day in the
	//emulator's zone for `for`, the one under way at the start included;  a time the clocks skip is the instant they jump to,
	//one they repeat its first.
	struct Recurrence{
		enum class EShape : uint8{ Every, Random, At };
		Recurrence( const jobject& o, sv owner, SRCE )ε;
		α IsActive( Duration t )Ι->bool{ return t>=Begin && t<End; }
		α NextEdge( Duration t )Ι->Duration{ return t<Begin ? Begin : End; }
		α Advance( Device& device )ι->void;//to the window after this one, once the clock is at its End.
		α Shortest()Ι->Duration{ return Shape==EShape::Random ? Min : Length; }
		EShape Shape;
		Duration Every{}, Mean{}, Min{}, Max{};
		Duration At{};//since the local midnight.
		Duration Length{};//`for`;  a random entry's, this window's draw.
		Duration Begin{}, End{};//both 0 until the first Sample advances to the first window.
	};
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
		vector<Stop> Stops;//the first active wins.
		vector<Slow> Slows;//as the stops.
		//A generated `status`'s own generator, which Sample reads before the stops;  the tag itself publishes On.
		up<IGenerator> StatusSource;
		bool On{ true };//`status` as of the last Sample:  the command, or StatusSource where `status` is generated, and off through a stop.
		UA_Int32 Code{};//the active stop's, else 0 - what `reasonCode` publishes.
		double Factor{ 1 };//the active slow period's, else 1.
		//A period:  the stops' clock, `status`, then every generated tag's Value and Status - a command tag has neither.  The period
		//is cut at each stop's and slow period's start and end, so each holds for its length whatever the period.
		α Sample( Duration dt )ι->void;
		optional<uint> StatusTag, ReasonTag;//indexes into Tags:  the bool tag named `status`;  `reasonCode`, which a stops list adds.
		Duration Clock{};
		TimePoint Start;//the wall time of Clock 0, which the {at, for} entries read.
		const std::chrono::time_zone* Zone{};//the emulator's.
		//The random entries' draws.  Seeded from `seed`, else from Start and the device's place in the list:  a std engine, so a
		//seed draws the same stops in every run, where a seeded absl::BitGen is salted per process.
		std::mt19937_64 Engine;
	};
	//Answers whether the contract publishes "<device>.<tag>" - PlcServer::FindField - and its field index when it does.
	//Empty for the write transport, where nothing is published and every generated tag goes over the session.
	using FindField = function<optional<uint>(sv name)>;
	///emulator/zone -> its zone.  Throws on a name the tz database lacks.
	α ParseZone( sv name, SRCE )ε->const std::chrono::time_zone&;
	///emulator/devices -> the devices, generators built, published fields mapped, Owner set.  Self is the caller's:
	//nothing here needs an Emulator, a session or a PLC server, which is what makes it a unit (T2).  A production tag with
	//a reject share is followed by its reject tag, and a `stops` list adds `reasonCode` as the last.  `start` is the wall time
	//of each device's Clock 0.  Throws on an empty list, a device without tags, any TagSpec refusal (#9), two tags of one name,
	//a `follows` that names no production tag of its device, a stop or slow period of no shape or two, stops on a device whose
	//`status` is not generated, slow periods on one with no production tag, and a command tag beside a generated `status`,
	//which would command nothing.  Owner and the `status` and `reasonCode` generators point into the vector, which never
	//grows after.
	α ParseDevices( const jarray& devices, const FindField& findField, const std::chrono::time_zone& zone, TimePoint start, SRCE )ε->vector<Device>;
}