#pragma once
#include "io/DayFiles.h"

namespace Jde::Opc::Hist{
	//The node's last record at or before through, as a read serves it, the edits applied:  none when the files hold
	//none.  The walk back (spec *Record format*) for a file made after its day, and for where an edit at or after the
	//node's newest record leaves it.  Reads the files alone, never the buffer, through files, under its lock.  Throws
	//when a file it opens can't be read through.
	α Last( GroupFiles& files, const std::chrono::time_zone& tz, uint readLimit, NodeIndex index, Ticks through, SRCE )ε->optional<Proto::DataValue>;
}