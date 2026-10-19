#pragma once
#include <jde/historian/Group.h>
#include <jde/fwk/io/records/Appender.h>
#include <jde/fwk/io/records/Scan.h>
#include <jde/fwk/io/records/Merge.h>

namespace Jde::Opc::Hist::Proto{//the record layer's two records, as the oneof embeds them.
	using FileStart = IO::Proto::FileStart;
	using Checkpoint = IO::Proto::Checkpoint;
}
namespace Jde::Opc::Hist{
	using Ticks = UA_DateTime;//a record's time:  100 ns since 1601.
	static_assert( std::is_same_v<Ticks,IO::Records::Time>, "The record layer's time is the historian's ticks." );
	Ξ ticks( Duration d )ι->Ticks{ return std::chrono::duration_cast<UATick>( d ).count(); }//a span in them.
	Ξ ticks( TimePoint t )ι->Ticks{ return UADateTime{ t }.UA(); }//a time in them.

	//A record's times are absolute in memory and deltas on disk (Hist.Records.proto).  ToDisk makes r's primary time a
	//delta from last and its other times deltas from its primary, then moves last on to its primary; ToMemory undoes it.
	//A FileStart sets last, and a Checkpoint leaves it.
	α ToDisk( Proto::HistoryRecord& r, Ticks& last )ι->void;
	α ToMemory( Proto::HistoryRecord& r, Ticks& last )ι->void;
	//The filing rule, what a record is filed and read by:  a value's source time, or its server's when it came with none,
	//and a membership change's time.  None for a FileStart, a Checkpoint, or a stored value with neither timestamp.
	α PrimaryTime( const Proto::HistoryRecord& r )ι->optional<Ticks>;
	α PrimaryTime( const Proto::DataValue& v )ι->optional<Ticks>;
	α PrimaryTime( const UA_DataValue& v )ι->Ticks;//Enqueue stamps a server time on one that came with neither.
	α PrimaryTime( const Record& r )ι->Ticks;

	//The built-in type id (Part 6 5.1.2) a type's values take on the wire, which a NodeAdded's data_type holds:  an
	//enumeration's Int32, a structure's ExtensionObject, and a subtype's its base's, as Duration's Double.  0 for none, and
	//for the types no DataType's values are all of:  Variant, which BaseDataType finds, DataValue and DiagnosticInfo.
	α BuiltIn( const UA_DataType* type )ι->uint32_t;
	α BuiltInType( uint32_t id )ι->const UA_DataType*;//BuiltIn's type, null for 0 or an id it never gives.
	//What a group buffered, as the file holds it, with absolute times.
	α ToProto( const Record& r )ε->Proto::HistoryRecord;
	//A buffered value the same way, its heartbeat marked, for its subscribers:  what ToProto( Record ) makes of one, but
	//with no warning, which is the flush's to give, so it can run under the group's lock (Enqueue never logs).
	α ToProto( const DataValue& value )ε->Proto::DataValue;
	//The edit's writer onto its record:  identity_id, UserPK::System as 4294967295, and user_name.
	α SetWriter( Proto::Modification& m, const Writer& by )ι->void;
	//Whether two stored values are one record:  the same times, status, value and heartbeat, a NaN the same as a NaN,
	//whichever node_index each carries.  A Modification's original is matched to the record it changed this way, and a
	//start value to the record it copies.
	α Same( const Proto::DataValue& a, const Proto::DataValue& b )ι->bool;
	//The most a record's member may take in memory, a DataValue or a NodeAdded:  the rest of the frame holds its tag and
	//length, and what its times can grow by as deltas, 9 bytes each at most.
	constexpr uint MaxMemberBytes{ IO::Records::MaxFrameBytes-64 };
	//A value with no file form, a DataValue or a DiagnosticInfo from a node typed BaseDataType, is stored without it, as
	//BadNotSupported, which ToProto( Record ) warns of once per node.  One with text that isn't UTF-8, which protobuf
	//writes but won't read back, is stored without it as BadEncodingError, warned of the same way.  Any other value that
	//can't be encoded is stored without it, with the status that says why and a warning each time, unless !warn, as is
	//one past MaxMemberBytes, as BadEncodingLimitsExceeded.
	α ToProto( const UA_DataValue& v, NodeIndex index={}, bool warn=true )ε->Proto::DataValue;

	//The framework's record layer (include/jde/fwk/io/records) over the historian's record:  its FileStart is the oneof's
	//field 1 and its Checkpoint field 6.  The layer frames, scans, merges and rewrites; what is the historian's own stays
	//here:  membership, start values, the delta times and the HistoryRecord oneof.
	constexpr uint32_t StartField{ 1 };
	constexpr uint32_t CheckpointField{ 6 };
	using IO::Records::Run;
	using IO::Records::Scanned;
	using IO::Records::EStop;
	using IO::Records::ENext;
	using IO::Records::Window;
}
namespace Jde::IO::Records{
	template<> struct Traits<Opc::Hist::Proto::HistoryRecord>{
		using R = Opc::Hist::Proto::HistoryRecord;
		static constexpr uint32_t StartField{ Opc::Hist::StartField };
		static constexpr uint32_t CheckpointField{ Opc::Hist::CheckpointField };
		Ω Known( uint32_t field )ι->bool{ return field>=R::kFileStart && field<=R::kIssued; }
		Ω Time( const R& r )ι->optional<Records::Time>{ return Opc::Hist::PrimaryTime( r ); }
		Ω ToDisk( R& r, State& chain )ι->void{ Opc::Hist::ToDisk( r, chain ); }
		Ω ToMemory( R& r, State& chain )ι->void{ Opc::Hist::ToMemory( r, chain ); }
		Ω SetStart( R& r, const Proto::FileStart& start )ι->void{ *r.mutable_file_start() = start; }
	};
}
namespace Jde::Opc::Hist{//after the traits, which the layer's templates are constrained on.
	using Appender = IO::Records::Appender<Proto::HistoryRecord>;
	using Reader = IO::Records::Reader<Proto::HistoryRecord>;
	using Decoder = IO::Records::Decoder<Proto::HistoryRecord>;
	using Merge = IO::Records::Merge<Proto::HistoryRecord>;
}