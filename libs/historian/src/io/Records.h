#pragma once
#include <jde/historian/Group.h>
DISABLE_WARNINGS
#include <jde/historian/proto/Hist.Records.pb.h>
ENABLE_WARNINGS

namespace Jde::Opc::Hist{
	using Ticks = UA_DateTime;//a record's time:  100 ns since 1601.

	//A record's times are absolute in memory and deltas on disk (Hist.Records.proto).  ToDisk makes r's primary time a
	//delta from last and its other times deltas from its primary, then moves last on to its primary; ToMemory undoes it.
	//A FileStart sets last, and a Checkpoint leaves it.
	α ToDisk( Proto::HistoryRecord& r, Ticks& last )ι->void;
	α ToMemory( Proto::HistoryRecord& r, Ticks& last )ι->void;
	//What an absolute record is filed and read by:  none for a FileStart, a Checkpoint, or a value with neither timestamp.
	α PrimaryTime( const Proto::HistoryRecord& r )ι->optional<Ticks>;

	//What a group buffered, as the file holds it, with absolute times.  A DataValue's break is not carried:  judging it
	//is the flush's.
	α ToProto( const Record& r )ε->Proto::HistoryRecord;
	//A value with no file form, a DataValue or a DiagnosticInfo from a node typed BaseDataType, is stored without it, with
	//the status that says why and a warning, instead of being lost silently.
	α ToProto( const UA_DataValue& v, NodeIndex index={} )ε->Proto::DataValue;
	α ToUA( const Proto::DataValue& v )ε->Value;

	//One append to a file, a run:  each record serialized delimited straight onto the end of out, with no copy of its own,
	//then a Checkpoint holding the CRC-32C of the run's bytes.
	struct Appender final{
		//chain is where the file's delta chain stands, from its first-open scan; a new file's FileStart sets it.
		Appender( string& out, Ticks chain )ι:_out{ out }, _start{ out.size() }, _chain{ chain }{}
		α Add( Proto::HistoryRecord&& r )ε->void;
		//Ends the run, and returns where the chain stands for the file's next.
		α Seal()ι->Ticks;
	private:
		α Write( const Proto::HistoryRecord& r )ε->void;
		string& _out;
		uint _start;
		Ticks _chain;
	};
}