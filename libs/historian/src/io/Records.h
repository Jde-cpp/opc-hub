#pragma once
#include <jde/historian/Group.h>

namespace Jde::Opc::Hist{
	using Ticks = UA_DateTime;//a record's time:  100 ns since 1601.
	Ξ ticks( Duration d )ι->Ticks{ return std::chrono::duration_cast<UATick>( d ).count(); }//a span in them.
	Ξ ticks( TimePoint t )ι->Ticks{ return UADateTime{ t }.UA(); }//a time in them.

	//A record's times are absolute in memory and deltas on disk (Hist.Records.proto).  ToDisk makes r's primary time a
	//delta from last and its other times deltas from its primary, then moves last on to its primary; ToMemory undoes it.
	//A FileStart sets last, and a Checkpoint leaves it.
	α ToDisk( Proto::HistoryRecord& r, Ticks& last )ι->void;
	α ToMemory( Proto::HistoryRecord& r, Ticks& last )ι->void;
	//A FileStart's crc:  the CRC-32C of its ts, generation and next_node_index, each little-endian.  Appender::Add sets it,
	//and the scan checks it before it trusts the time or the generation.
	α StartCrc( const Proto::FileStart& start )ι->uint32_t;
	//The longest a FileStart record's body can be, every field at its widest:  a first length past it is no torn preamble.
	constexpr uint32_t MaxFileStartBody{ 30 };
	//The filing rule, what a record is filed and read by:  a value's source time, or its server's when it came with none,
	//and a membership change's time.  None for a FileStart, a Checkpoint, or a stored value with neither timestamp.
	α PrimaryTime( const Proto::HistoryRecord& r )ι->optional<Ticks>;
	α PrimaryTime( const Proto::DataValue& v )ι->optional<Ticks>;
	α PrimaryTime( const UA_DataValue& v )ι->Ticks;//Enqueue stamps a server time on one that came with neither.
	α PrimaryTime( const Record& r )ι->Ticks;

	//What a group buffered, as the file holds it, with absolute times.
	α ToProto( const Record& r )ε->Proto::HistoryRecord;
	//A value with no file form, a DataValue or a DiagnosticInfo from a node typed BaseDataType, is stored without it, as
	//BadNotSupported, which ToProto( Record ) warns of once per node.  One with text that isn't UTF-8, which protobuf
	//writes but won't read back, is stored without it as BadEncodingError, warned of the same way.  Any other value that
	//can't be encoded is stored without it, with the status that says why and a warning each time.
	α ToProto( const UA_DataValue& v, NodeIndex index={} )ε->Proto::DataValue;

	//One append to a file, a run:  each record serialized delimited straight onto the end of out, with no copy of its own,
	//then a Checkpoint holding the CRC-32C of the run's bytes.
	struct Appender final{
		//chain is where the file's delta chain stands, from its first-open scan; a new file's FileStart sets it.
		Appender( string& out, Ticks chain )ι:_out{ out }, _start{ out.size() }, _chain{ chain }{}
		//Throws for a record over protobuf's 2 GB limit, or when out can't grow, leaving out and the chain as they were, so
		//the run can go on without r.
		α Add( Proto::HistoryRecord&& r )ε->void;
		//Ends the run, and returns where the chain stands for the file's next.  Throws only when out can't grow, changing
		//nothing.
		α Seal()ε->Ticks;
	private:
		α Write( const Proto::HistoryRecord& r )ε->void;
		string& _out;
		uint _start;
		Ticks _chain;
	};
}