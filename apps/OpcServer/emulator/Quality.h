#pragma once
#include "Recurrence.h"

namespace Jde::Opc::Emulator{
	//A reading's data quality - OPC 10000-4 §7.38.  A StatusCode's layout (Table 176, and Table 177 for the info bits):
	//  31:30 Severity (00 Good, 01 Uncertain, 10 Bad, 11 reserved) | 27:16 SubCode | 15 StructureChanged | 14 SemanticsChanged
	//  | 11:10 InfoType (01 = DataValue: the bits below mean something) | 9:8 LimitBits | 7 Overflow | 4:0 historian bits.
	//The historian bits describe stored data, not a device's live reading, and are not emulated.
	enum class ELimit : uint8{ None, Low, High, Constant };//the 9:8 field's values, in order.
	α ParseLimit( sv s, SRCE )ε->ELimit;
	struct StatusFlags{ ELimit Limit{}; bool Overflow{}, StructureChanged{}, SemanticsChanged{}; };
	//ORs the flags into `code`;  a limit replaces the code's own 9:8, and InfoType becomes DataValue once a limit or
	//overflow bit is set - without it a reader is told to ignore them.
	α Compose( UA_StatusCode code, StatusFlags flags )ι->UA_StatusCode;
	//A name from StatusNames - spelled as UA_StatusCode_name spells it, "BadSensorFailure" - or any code by number,
	//"0x808C0000" | "2156658688", which is how the rest of Table 178 is reached.  Refuses the reserved severity 11.
	α ParseStatus( sv nameOrNumber, SRCE )ε->UA_StatusCode;
	α StatusNames()ι->std::span<const std::pair<sv,UA_StatusCode>>;//the qualities a device produces about its own readings.
	α ToString( UA_StatusCode sc )ι->string;//"BadSensorFailure", "UncertainEngineeringUnitsExceeded+High".

	//One scheduled fault:  `Status` through each of its windows, in any of Recurrence's shapes, on the device's clock.
	struct QualityWindow : Recurrence{
		QualityWindow( const jobject& o, sv tagName, SRCE )ε;
		UA_StatusCode Status{};//composed: the flags are already in it.
		bool Hold{};//publish the last reading from before the window - a blind sensor.  Defaults on for Bad and the two LastUsableValue codes.
		bool HasLimit{};//the window names its own LimitBits, so the sensor range's are not ORed in.
	};

	//A tag's quality over time:  the first active window wins;  outside the windows a reading pinned at the sensor range is
	//UncertainEngineeringUnitsExceeded with the Low/High limit bit;  otherwise Good.
	struct TagQuality{
		TagQuality()ι = default;
		//The random windows draw from an engine of the tag's own, seeded from `seed`, the device's, and `place`, the tag's in its
		//list:  a tag gaining a window leaves the device's stops where they were, and a seed draws the same windows every run.
		TagQuality( vector<QualityWindow> windows, optional<double> sensorMin, optional<double> sensorMax, uint64_t seed=0, uint32_t place=0 )ι;
		α Advance( const Device& device )ι->void;//each window past its End on to the next - to its first at the parse;  Apply does it too.
		α Anchor( Duration drift, const Device& device )ι->void;//the `at` windows with the wall clock, by `drift`, what Device::Anchor moved Start by.
		//At the device's clock:  `raw` is the generator's sample, `value` the last published reading - left alone while a window
		//holds, else set to `raw` clamped to the sensor range.  The generator is sampled either way - the process runs on, the
		//sensor is blind.
		α Apply( const Device& device, double raw, double& value )ι->UA_StatusCode;
	private:
		vector<QualityWindow> _windows;
		optional<double> _sensorMin, _sensorMax;
		std::mt19937_64 _engine;
		ELimit _clamp{};//of the last live reading;  a held reading keeps the limit it was taken at.
	};
}