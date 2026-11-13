#pragma once
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/openProtocol/Mids.h>

//MID 0061 as the variables of `Result` (docs/open-protocol/spec.md *Address space*):  one per field of revisions 1 to 7 under the
//type's own names, but the cell id, the channel id and the controller name, which `_Op.Identity` carries, and revision 7's final
//angle decimal, which is `Angle` where it is sent.  A tightening id is a UInt32, a time a DateTime read in the row's zone, the
//tightening status a Boolean, true for OK, a coded status, the strategy, the unit and the result type a Byte of the code, a
//torque and an angle a Double, an error status and the strategy options a UInt32 of their bits, a count a UInt16 and a name a
//String.  An old result, MID 0065's (Tables 123 to 129), is the same variables but those it does not carry:  the limits, the
//parameter set's name, the time of its last change and the compensated angle, with `Angle` in whole degrees.  No value here
//carries a timestamp:  the driver gives a result's values its one.
namespace Jde::Opc::Gateway::TighteningResult{
	struct Variable{
		sv Name;
		uint8_t Since;//the first revision of MID 0061 that carries it, Tables 108 to 114.
		uint8_t StoredSince;//the first of MID 0065, Tables 123 to 129, and 0 where none does:  a limit, the parameter set's name and the time of its last change.
		const UA_DataType* Type;
	};
	//Every variable in the order a result is written, TighteningId last:  it changes with every result, so a client that has
	//it has the result whole.
	α Variables()ι->const vector<Variable>&;
	//The scalar as a value of its own, with `status` where it is not Good, and no timestamps.
	α Scalar( const void* scalar, const UA_DataType& type, UA_StatusCode status=UA_STATUSCODE_GOOD )ι->Value;
	//`type`'s zero under `status`:  open62541 refuses a typed variable a value of none, whatever its status.
	α Unset( const UA_DataType& type, UA_StatusCode status )ι->Value;
	//`local`, whole seconds of a clock that keeps `zone`, as UTC.  An hour that comes twice is the pass nearer `seen`, when it
	//was read:  a controller sends a result seconds after it is made, and the passes are an hour apart.  A `stored` result,
	//MID 0065's, is read long after it was made, so its hour is the later pass where that is not after `seen`, the time of
	//the result made after it.
	α ToSys( OpenProtocol::LocalTime local, const std::chrono::time_zone& zone, TimePoint seen, bool stored=false )ι->TimePoint;
	//Variables' each, in its order:  the result's value where `revision` carries the variable, Bad_NotSupported over its zero
	//where the result sent none, its field all spaces (§2.3, Table 3), and nothing where it does not.  A name's empty text is
	//a value.  Its times are read as ToSys reads them.
	α Values( const OpenProtocol::Mid0061& result, uint16_t revision, const std::chrono::time_zone& zone, TimePoint seen )ι->vector<optional<Value>>;
	//The same of an old result, by MID 0065's revision, its time read as a stored one's by `by`.
	α Values( const OpenProtocol::Mid0065& stored, uint16_t revision, const std::chrono::time_zone& zone, TimePoint by )ι->vector<optional<Value>>;
}