#pragma once
#include "Quality.h"

namespace Jde::Opc::Emulator{
	//How a tag's value evolves.  Free-running modes ignore the device's rate; Follow and Production track it; On is a bool that
	//stays on, a `status` the device's stops alone hold off; Command tags are never written by the emulator - the UI owns them
	//and the emulator subscribes.
	enum class EMode : uint8{ Sine, Ramp, RandomWalk, Counter, Toggle, On, Command, Follow, Production };
	α ParseMode( sv s, SRCE )ε->EMode;
	α ToString( EMode mode )ι->sv;

	struct TagSpec{
		TagSpec( const jobject& o, SRCE )ε;//throws on a key there but of the wrong kind (FindKey), and on the refusals below it.
		α IsBool()Ι->bool{ return Mode==EMode::Toggle || Mode==EMode::On || Mode==EMode::Command; }
		string Name;
		EMode Mode{ EMode::Counter };
		double Min{ 0 }, Max{ 100 }, Step{ 1 }, RatedRpm{ 1450 };
		Duration Period{ 30s }, Tau{ 3s };
		string Follows;//follow:  the device's production tag whose pace it lags towards, in units a minute, where RatedRpm would be;  none, RatedRpm.
		Duration Cycle{ 1s }, Jitter{};//production:  a unit every Cycle, give or take Jitter, while the device's status is on.
		double Reject{};//production:  the share of the units counted on RejectTag too;  0, no second tag.
		string RejectTag{ "rejectCount" };
		optional<double> SensorMin, SensorMax;//the transmitter's range: a reading outside it is published pinned there, with the limit bit (Quality.h).
		vector<QualityWindow> Quality;//the scheduled faults, first active wins.
	};

	//One tag's value generator.  `dt` is the time since the previous sample, `rate` the share of its pace the device ran at through
	//it:  0 off, 1 on, 1/factor in a slow period - Device::Sample cuts a period at each stop's and slow period's edges, so a period
	//has one rate.
	struct IGenerator{
		virtual ~IGenerator() = default;
		β Next( Duration dt, double rate )ι->double = 0;
		β PerPeriod()Ι->bool{ return false; }//steps once a period, whatever `dt`:  sampled once a period, not once a piece.
	};
	α MakeGenerator( const TagSpec& spec )ε->up<IGenerator>;//production:  the count alone.
	//production's pair:  the count, and the rejects among the units counted so far - sampled after the count.
	struct Production{ up<IGenerator> Count, Rejects; };
	α MakeProduction( const TagSpec& spec )ε->Production;
}