#pragma once
#include <jde/fwk/io/records/Appender.h>
#include <jde/fwk/io/records/Merge.h>
#include <jde/fwk/io/records/Scan.h>
#include <jde/flows/FileTrace.h>
DISABLE_WARNINGS
#include <jde/flows/proto/Flow.Records.pb.h>
ENABLE_WARNINGS

//The framework's record layer (include/jde/fwk/io/records) over the trace's record (Flow.Records.proto):  its FileStart is the
//oneof's file_start and its Checkpoint its checkpoint.  The layer frames, scans, merges and appends;  what is the trace's own
//is here:  the time in ns since the epoch, a delta on disk as the historian's ticks are, and the conversions between the
//library's TraceRecord and the file's Hop, a minute's figures and the file's Counts.
namespace Jde::Opc::Flows{
	constexpr uint32_t StartField{ Proto::FlowRecord::kFileStartFieldNumber };
	constexpr uint32_t CheckpointField{ Proto::FlowRecord::kCheckpointFieldNumber };
	using IO::Records::Run;
	using IO::Records::ENext;
	using IO::Records::EStop;

	Ξ Ns( TimePoint t )ι->int64_t{ return std::chrono::duration_cast<std::chrono::nanoseconds>( t.time_since_epoch() ).count(); }
	Ξ FromNs( int64_t ns )ι->TimePoint{ return TimePoint{ std::chrono::duration_cast<Duration>(std::chrono::nanoseconds{ns}) }; }
	//A record's time, a Hop's or a Counts':  none for the layer's two.
	α Time( const Proto::FlowRecord& r )ι->optional<int64_t>;
	//A record's time is absolute in memory and a delta from the record before on disk;  a FileStart sets `last`.
	α ToDisk( Proto::FlowRecord& r, int64_t& last )ι->void;
	α ToMemory( Proto::FlowRecord& r, int64_t& last )ι->void;
	//A record as the file holds it:  its json cut at valueLimit on a UTF-8 boundary, and the record flagged.
	α ToProto( const TraceRecord& r, uint valueLimit )ι->Proto::FlowRecord;
	α ToProto( TimePoint minute, const NodeCounts& node, uint32 run=0 )ι->Proto::FlowRecord;
	α FromProto( const Proto::Hop& hop )ι->TraceRecord;
	α FromProto( const Proto::Counts& counts )ι->CountRow;
}
namespace Jde::IO::Records{
	template<> struct Traits<Opc::Flows::Proto::FlowRecord>{
		using R = Opc::Flows::Proto::FlowRecord;
		static constexpr uint32_t StartField{ Opc::Flows::StartField };
		static constexpr uint32_t CheckpointField{ Opc::Flows::CheckpointField };
		Ω Known( uint32_t field )ι->bool{ return field>=R::kFileStart && field<=R::kCheckpoint; }
		Ω Time( const R& r )ι->optional<Records::Time>{ return Opc::Flows::Time( r ); }
		Ω ToDisk( R& r, State& chain )ι->void{ Opc::Flows::ToDisk( r, chain ); }
		Ω ToMemory( R& r, State& chain )ι->void{ Opc::Flows::ToMemory( r, chain ); }
		Ω SetStart( R& r, const Proto::FileStart& start )ι->void{ *r.mutable_file_start() = start; }
	};
}
namespace Jde::Opc::Flows{//after the traits, which the layer's templates are constrained on.
	using Appender = IO::Records::Appender<Proto::FlowRecord>;
	using Reader = IO::Records::Reader<Proto::FlowRecord>;
	using Decoder = IO::Records::Decoder<Proto::FlowRecord>;
	using Merge = IO::Records::Merge<Proto::FlowRecord>;
}