#pragma once

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

	//One scheduled fault: `Status` from `Start` for `Length`, again every `Every` when set.  Times are on the tag's own
	//clock, which starts with the emulator.
	struct QualityWindow{
		QualityWindow( const jobject& o, sv tagName, SRCE )ε;
		α IsActive( Duration t )Ι->bool;
		UA_StatusCode Status{};//composed: the flags are already in it.
		Duration Start{}, Length{};
		optional<Duration> Every;
		bool Hold{};//publish the last reading from before the window - a blind sensor.  Defaults on for Bad and the two LastUsableValue codes.
		bool HasLimit{};//the window names its own LimitBits, so the sensor range's are not ORed in.
	};

	//A tag's quality over time:  the first active window wins;  outside the windows a reading pinned at the sensor range is
	//UncertainEngineeringUnitsExceeded with the Low/High limit bit;  otherwise Good.
	struct TagQuality{
		TagQuality()ι = default;
		TagQuality( vector<QualityWindow> windows, optional<double> sensorMin, optional<double> sensorMax )ι;
		//`raw` is the generator's sample, `value` the last published reading:  left alone while a window holds, else set to
		//`raw` clamped to the sensor range.  The generator is sampled either way - the process runs on, the sensor is blind.
		α Apply( Duration dt, double raw, double& value )ι->UA_StatusCode;
	private:
		vector<QualityWindow> _windows;
		optional<double> _sensorMin, _sensorMax;
		Duration _t{};
		ELimit _clamp{};//of the last live reading;  a held reading keeps the limit it was taken at.
	};
}
