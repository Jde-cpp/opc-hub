#pragma once
#include <jde/historian/Group.h>

namespace Jde::Opc::Hist{
	//The compression test:  whether change is stored after stored, its node's last stored value.  It is when the status
	//code changed, or the value left the deviation band around stored's:  ExceptionDeviation in the value's units, as a
	//percentage of stored's magnitude, so at zero every change passes, or as a percentage of the node's range.  A change
	//of exactly the deviation has left the band, so a deviation of 0 stores every change.  The band is for numeric scalars
	//of one type:  anything else passes, as does a value that is not a number.
	α Passes( const Thresholds& config, const UA_DataValue& stored, const UA_DataValue& change )ι->bool;
	//Whether change repeats stored:  the same status code, and the same value on the wire.  That is how a value read back
	//from a file compares with the one collected, an alias being its built-in type there and a structure encoded.
	α Repeats( const UA_DataValue& stored, const UA_DataValue& change )ι->bool;
}
