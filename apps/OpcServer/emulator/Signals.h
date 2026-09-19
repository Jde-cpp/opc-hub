#pragma once
#include "Quality.h"

namespace Jde::Opc::Emulator{
	//How a tag's value evolves.  Free-running modes ignore the device's run command; Follow tracks it; Command tags are
	//never written by the emulator - the UI owns them and the emulator subscribes.
	enum class EMode : uint8{ Sine, Ramp, RandomWalk, Counter, Toggle, Command, Follow };
	α ParseMode( sv s, SRCE )ε->EMode;
	α ToString( EMode mode )ι->sv;

	struct TagSpec{
		TagSpec( const jobject& o, SRCE )ε;
		α IsBool()Ι->bool{ return Mode==EMode::Toggle || Mode==EMode::Command; }
		string Name;
		EMode Mode{ EMode::Counter };
		double Min{ 0 }, Max{ 100 }, Step{ 1 }, RatedRpm{ 1450 };
		Duration Period{ 30s }, Tau{ 3s };
		optional<double> SensorMin, SensorMax;//the transmitter's range: a reading outside it is published pinned there, with the limit bit (Quality.h).
		vector<QualityWindow> Quality;//the scheduled faults, first active wins.
	};

	//One tag's value generator.  `dt` is the time since the previous sample, `command` the device's run command.
	struct IGenerator{
		virtual ~IGenerator() = default;
		β Next( Duration dt, bool command )ι->double = 0;
	};
	α MakeGenerator( const TagSpec& spec )ε->up<IGenerator>;
}
