#pragma once
#include <jde/opc/uatypes/Value.h>
#include <jde/opc/openProtocol/Mids.h>

//MID 0061 as the variables of `Result` (docs/open-protocol/spec.md *Address space*):  one per field of revisions 1 to 7 under the
//type's own names, but the cell id, the channel id and the controller name, which `_Op.Identity` carries, and revision 7's final
//angle decimal, which is `Angle` where it is sent.  A tightening id is a UInt32, a time a DateTime read in the row's zone, the
//tightening status a Boolean, true for OK, a coded status, the strategy, the unit and the result type a Byte of the code, a
//torque and an angle a Double, an error status and the strategy options a UInt32 of their bits, a count a UInt16 and a name a
//String.  No value here carries a timestamp:  the driver gives a result's values its one.
namespace Jde::Opc::Gateway::TighteningResult{
	struct Variable{
		sv Name;
		uint8_t Since;//the first revision of MID 0061 that carries it, Tables 108 to 114.
		const UA_DataType* Type;
	};
	//Every variable in the order a result is written, TighteningId last:  it changes with every result, so a client that has
	//it has the result whole.
	α Variables()ι->const vector<Variable>&;
	//`type`'s zero under `status`:  open62541 refuses a typed variable a value of none, whatever its status.
	α Unset( const UA_DataType& type, UA_StatusCode status )ι->Value;
	//The variables `revision` carries, in Variables' order, each with the result's value, or Bad_NotSupported where the result
	//sent none, its field all spaces (§2.3, Table 3).  A name's empty text is a value.
	α Values( const OpenProtocol::Mid0061& result, uint16_t revision, const std::chrono::time_zone& zone )ι->vector<std::pair<sv,Value>>;
}