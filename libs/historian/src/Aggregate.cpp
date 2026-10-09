#include <jde/historian/Group.h>
#include <cmath>
#include <absl/container/flat_hash_map.h>
#include <absl/functional/function_ref.h>
#include <jde/fwk/io/crc.h>
#include <jde/opc/UAException.h>
#include "Store.h"
#include "io/DayFiles.h"
#include "io/Records.h"
DISABLE_WARNINGS
#include <jde/historian/proto/Hist.Read.pb.h>
#include <google/protobuf/io/coded_stream.h>
ENABLE_WARNINGS

#define let const auto

//ReadAtTime and ReadProcessed (spec *Reads*):  Part 11's at-time read and Part 13's aggregates, computed from the raw
//records as Read serves them.  A page makes one pass over the raw records of its range, the bounds asked for, and as
//the stream passes each time it needs a value at, a boundary, it notes the node's records on either side of it, which
//the bounding value there is read off, interpolated or simple (Part 13 §3.1.8, §3.1.9).  A processed read also sorts
//each record into its interval's bucket, which its aggregate is computed from once the pass is done.  The first Good
//value before a Bad opening bound, the second before for a sloped extrapolation, and the first after a Bad closing bound
//lie outside the range, so a probe reads for them, one page of readLimit records, which bounds the search.
namespace Jde::Opc::Hist{
	using google::protobuf::io::CodedOutputStream;

	//Part 4's Proto::DataValue info bits, which Part 11 and Part 13 set on a history value's status:  the historian bits
	//count only with the InfoType set to Proto::DataValue.
	constexpr StatusCode InfoDataValue{ 0x400 }, Calculated{ 0x1 }, Interpolated{ 0x2 }, Partial{ 0x4 }, MultiValue{ 0x10 };
	constexpr StatusCode Good{ UA_STATUSCODE_GOOD }, Bad{ 0x80000000 }, SubNormal{ UA_STATUSCODE_UNCERTAINDATASUBNORMAL }, NoData{ UA_STATUSCODE_BADNODATA };
	constexpr StatusCode InvalidInputs{ UA_STATUSCODE_BADAGGREGATEINVALIDINPUTS };
	constexpr Ticks Earliest{ std::numeric_limits<Ticks>::min() }, Latest{ std::numeric_limits<Ticks>::max() };
	//From a time to one no earlier, unsigned:  the signed difference of two times can pass Int64's range.
	Ω ticksBetween( Ticks from, Ticks to )ι->uint64_t{ return (uint64_t)to-(uint64_t)from; }
	Ω withBits( StatusCode status, StatusCode bits )ι->StatusCode{ return bits ? status | InfoDataValue | bits : status; }
	enum class EMode : uint8{ AtTime=1, Processed=2 };

	//How an aggregate takes a raw value's status:  Bad, or Uncertain under TreatUncertainAsBad, is left out of every
	//calculation and makes the result Uncertain; Uncertain otherwise is a bounding value, and used by a time average,
	//but no Good value.
	enum class EClass : uint8{ Good, Uncertain, Bad };
	Ω classify( StatusCode status, bool treatUncertainAsBad )ι->EClass{
		if( UA_StatusCode_isBad(status) )
			return EClass::Bad;
		if( UA_StatusCode_isUncertain(status) )
			return treatUncertainAsBad ? EClass::Bad : EClass::Uncertain;
		return EClass::Good;
	}
	Ω number( const Opc::Proto::Value& v )ι->optional<double>{
		switch( v.of_case() ){
		case Opc::Proto::Value::kDoubleValue: return v.double_value();
		case Opc::Proto::Value::kFloatValue: return (double)v.float_value();
		case Opc::Proto::Value::kSbyte: return (double)v.sbyte();
		case Opc::Proto::Value::kInt16: return (double)v.int16();
		case Opc::Proto::Value::kInt32: return (double)v.int32();
		case Opc::Proto::Value::kInt64: return (double)v.int64();
		case Opc::Proto::Value::kByte: return (double)v.byte();
		case Opc::Proto::Value::kUint16: return (double)v.uint16();
		case Opc::Proto::Value::kUint32: return (double)v.uint32();
		case Opc::Proto::Value::kUint64: return (double)v.uint64();
		default: return nullopt;
		}
	}
	Ω number( const Proto::DataValue& v )ι->optional<double>{ return v.has_value() ? number( v.value() ) : nullopt; }
	//d as the source's type, for a result Part 13 types "Same as Source":  an integer rounds to the nearest inside
	//its range.
	Ω numberAs( double d, const Opc::Proto::Value& like )ι->Opc::Proto::Value{
		let rounded = [d]( double low, double high ){ return std::llround( std::clamp(d, low, high) ); };
		Opc::Proto::Value y;
		switch( like.of_case() ){
		case Opc::Proto::Value::kFloatValue: y.set_float_value( (float)d ); break;
		case Opc::Proto::Value::kSbyte: y.set_sbyte( (int32_t)rounded(-128, 127) ); break;
		case Opc::Proto::Value::kInt16: y.set_int16( (int32_t)rounded(-32768, 32767) ); break;
		case Opc::Proto::Value::kInt32: y.set_int32( (int32_t)rounded(-2147483648.0, 2147483647.0) ); break;
		case Opc::Proto::Value::kInt64: y.set_int64( rounded((double)std::numeric_limits<int64_t>::min(), (double)std::numeric_limits<int64_t>::max()) ); break;
		case Opc::Proto::Value::kByte: y.set_byte( (uint32_t)rounded(0, 255) ); break;
		case Opc::Proto::Value::kUint16: y.set_uint16( (uint32_t)rounded(0, 65535) ); break;
		case Opc::Proto::Value::kUint32: y.set_uint32( (uint32_t)rounded(0, 4294967295.0) ); break;
		case Opc::Proto::Value::kUint64: y.set_uint64( d<=0 ? 0 : d>=18446744073709551615.0 ? std::numeric_limits<uint64_t>::max() : (uint64_t)std::llround(d) ); break;
		default: y.set_double_value( d ); break;
		}
		return y;
	}

	//A raw record as the stream passed it to a node's track.
	struct Seen{ Ticks Time; Proto::DataValue Value; EClass Class; };
	//A node's raw records around one time (Part 13 §3.1.8 and §3.1.9), as the stream saw them on either side of it
	//once it passed:  what the bounding value there is read off.
	struct Boundary{
		Ticks Time;
		optional<Seen> At;//the last non-Bad record at Time, which is the bounding value itself.
		optional<Seen> AtAny;//the last record at Time, of any status:  a simple bound.
		bool BadAt{};//a Bad record at Time.
		optional<Seen> Before, Before2;//the last two non-Bad records before Time.
		bool Before2Unknown{};//Before came from the opening bound, so Before2 lies before the range:  a probe finds it.
		optional<Seen> BeforeAny;//the last record before Time, of any status.
		bool BadBetweenBefore{};//a Bad record after Before and no later than Time.
		optional<Seen> After, AfterAny;//the first non-Bad record after Time, and the first of any status.
		bool BadBetweenAfter{};//a Bad record after Time and before After.
		bool AnyAfter{};//a record of any status after Time:  without one, Time is past the end of the data.
	};
	//A node as one pass sees it:  its boundaries, in time order, and what the stream has passed, which the next
	//boundary takes as the records before it.
	struct Track{
		NodeIndex Index{};
		vector<Boundary> Boundaries;
		uint Settled{}, Resolved{}, Followed{};//how many from the front are settled, have their After, and have had any record after them.
		optional<Ticks> CurrentTime;//the latest time seen, whose records are Current until a later one comes.
		vector<Seen> Current;
		optional<Seen> Prev, Prev2, PrevAny;//the last two non-Bad records before CurrentTime, and the last of any status.
		bool Prev2Unknown{};
		bool BadSincePrev{};//a Bad record after Prev, before CurrentTime.
		optional<Ticks> FirstSeen, LastSeen;//the first and last record the pass saw of the node, the opening bound included.
		optional<Seen> Closing;//the separate closing bound:  the first record after the range.
		//Whether the node has a record before the range, and one after it:  unknown while a record at the range's own
		//end served as its bound, which a probe then settles.
		optional<bool> AnyBefore, AnyAfter;
		bool BeforeUnknown{};//the first record came at the range's start, as its bound, so what lies before is unknown until probed.
		bool ClosingKnown{};
	};

	//The reads outside the range:  for the first non-Bad records before a time and the first after.  Each reads at
	//most readLimit records, which bounds the search, as Part 13 leaves to the server.
	struct Prober final{
		Group& Group_;
		uint ReadLimit;
		bool TreatUncertainAsBad;
		SL Sl;
		//The records r reads, to each until it answers false:  in pages from 16 records, each four times the last, since
		//the first usually holds the answer, up to ReadLimit in all.
		α Walk( ReadRequest r, absl::FunctionRef<bool( const ReadValue& )> each )ε->void{
			uint read{};
			for( uint limit=16; read<ReadLimit; limit*=4 ){
				r.Limit = std::min( limit, ReadLimit-read );
				auto page = Group_.Read( r, Sl );
				read += page.Values.size();
				for( let& v : page.Values ){
					if( !each(v) )
						return;
				}
				if( page.Continuation.empty() )
					return;
				r.Continuation = move( page.Continuation );
			}
		}
		//The non-Bad records before time, nearest first, up to count of them; whether a Bad one lies between the
		//first and time, and whether any record does.  With atTime the records at time come too, the latest first,
		//behind a Bad bound there, and past the first non-Bad when that is the record whose predecessors are wanted.
		struct Behind{ vector<Seen> Found; bool SkippedBad{}; bool Any{}; };
		α Back( NodeIndex index, Ticks time, uint count, bool atTime=false, bool pastFirstNonBad=false )ε->Behind{
			Behind y;
			if( time==Earliest && !atTime )
				return y;
			bool skip{ pastFirstNonBad };
			Walk( {.Nodes={index}, .End=atTime ? time : time-1}, [&]( const ReadValue& v ){
				let c = classify( v.Value.status(), TreatUncertainAsBad );
				if( c!=EClass::Bad && std::exchange(skip, false) )
					return true;
				y.Any = true;
				if( c!=EClass::Bad ){
					y.Found.push_back( {*PrimaryTime(v.Value), v.Value, c} );
					return y.Found.size()<count;
				}
				if( y.Found.empty() )
					y.SkippedBad = true;
				return true;
			});
			return y;
		}
		//Whether the node has a record before time, or after it.
		α Exists( NodeIndex index, Ticks time, bool before )ε->bool{
			if( time==(before ? Earliest : Latest) )
				return false;
			ReadRequest r{ .Nodes={index}, .Limit=1 };
			( before ? r.End : r.Start ) = before ? time-1 : time+1;
			return !Group_.Read( r, Sl ).Values.empty();
		}
		//The first record after time, and the first non-Bad one; with atTime those at time come too, past the Bad
		//bound there, which is the first.
		struct Ahead{ optional<Seen> First, NonBad; };
		α Forward( NodeIndex index, Ticks time, bool atTime=false )ε->Ahead{
			Ahead y;
			if( time==Latest && !atTime )
				return y;
			bool skip{ atTime };
			Walk( {.Nodes={index}, .Start=atTime ? time : time+1}, [&]( const ReadValue& v ){
				if( std::exchange(skip, false) )
					return true;
				let c = classify( v.Value.status(), TreatUncertainAsBad );
				Seen s{ *PrimaryTime(v.Value), v.Value, c };
				if( !y.First )
					y.First = s;
				if( c==EClass::Bad )
					return true;
				y.NonBad = move( s );
				return false;
			});
			return y;
		}
	};

	//The opening bound, the node's last record before the range:  what the first boundary takes as before it.  After
	//a Bad one the non-Bad records before are probed for.
	Ω open( Track& t, Seen&& s, Prober& prober )ε->void{
		t.FirstSeen = t.LastSeen = s.Time;
		t.AnyBefore = true;
		t.PrevAny = s;
		if( s.Class!=EClass::Bad ){
			t.Prev = move( s );
			t.Prev2Unknown = true;
			return;
		}
		t.BadSincePrev = true;
		if( t.Boundaries.empty() )//only a boundary reads the records before.
			return;
		auto behind = prober.Back( t.Index, s.Time, 2, true );
		if( behind.Found.size()>1 )
			t.Prev2 = move( behind.Found[1] );
		if( !behind.Found.empty() )
			t.Prev = move( behind.Found[0] );
	}
	//What lies before a node's first record when that came at the range's start, as its own bound:  probed for
	//once a boundary needs it.
	Ω resolveBefore( Track& t, Prober& prober )ε->void{
		t.BeforeUnknown = false;
		auto behind = prober.Back( t.Index, *t.FirstSeen, 2 );
		t.AnyBefore = behind.Any;
		t.BadSincePrev = t.BadSincePrev || behind.SkippedBad;
		if( behind.Found.size()>1 )
			t.Prev2 = move( behind.Found[1] );
		if( !behind.Found.empty() )
			t.Prev = move( behind.Found[0] );
	}
	//The records at CurrentTime into what came before:  the boundaries after it take them as before.
	Ω fold( Track& t )ι->void{
		if( !t.Current.empty() )
			t.PrevAny = t.Current.back();
		for( auto& s : t.Current ){
			if( s.Class==EClass::Bad ){
				t.BadSincePrev = true;
				continue;
			}
			t.Prev2 = move( t.Prev );
			t.Prev2Unknown = !t.Prev2 && t.BeforeUnknown;
			t.Prev = move( s );
			t.BadSincePrev = false;
		}
		t.Current.clear();
		t.CurrentTime.reset();
	}
	//Settles each boundary before upTo, or every one at the pass's end, Latest's included:  the stream has passed it,
	//so the records at it and before it are known.
	Ω advance( Track& t, optional<Ticks> upTo, Prober& prober )ε->void{
		for( ; t.Settled<t.Boundaries.size() && (!upTo || t.Boundaries[t.Settled].Time<*upTo); ++t.Settled ){
			auto& b = t.Boundaries[t.Settled];
			//The records before the first are unknown, and none at hand is the bound or the value before it.
			let currentNonBad = t.CurrentTime && *t.CurrentTime<=b.Time && std::ranges::any_of( t.Current, []( let& s ){ return s.Class!=EClass::Bad; } );
			if( t.BeforeUnknown && !t.Prev && !currentNonBad )
				resolveBefore( t, prober );
			if( t.CurrentTime && *t.CurrentTime==b.Time ){
				for( let& s : t.Current ){
					if( s.Class==EClass::Bad )
						b.BadAt = true;
					else
						b.At = s;
				}
				b.AtAny = t.Current.back();
			}
			else if( t.CurrentTime && *t.CurrentTime<b.Time )
				fold( t );
			b.Before = t.Prev;
			b.Before2 = t.Prev2;
			b.Before2Unknown = t.Prev2Unknown;
			b.BeforeAny = t.PrevAny;
			b.BadBetweenBefore = t.BadSincePrev || (!b.At && b.BadAt);
		}
		if( t.CurrentTime && (!upTo || *t.CurrentTime<*upTo) )
			fold( t );
	}
	//s, after every settled boundary still without its After.  Those settled since the last record take it as their
	//first after; the others without an After have had only Bad ones, so a Bad s is nothing new to them.
	Ω afterOf( Track& t, const Seen& s )ι->void{
		let bad = s.Class==EClass::Bad;
		for( ; t.Followed<t.Settled; ++t.Followed ){
			auto& b = t.Boundaries[t.Followed];
			b.AnyAfter = true;
			b.AfterAny = s;
			b.BadBetweenAfter = bad;
		}
		if( bad )
			return;
		for( ; t.Resolved<t.Settled; ++t.Resolved )
			t.Boundaries[t.Resolved].After = s;
	}
	Ω feed( Track& t, Seen&& s, Prober& prober )ε->void{
		if( !t.FirstSeen )
			t.FirstSeen = s.Time;
		t.LastSeen = s.Time;
		if( !t.CurrentTime || *t.CurrentTime!=s.Time ){
			advance( t, s.Time, prober );
			t.CurrentTime = s.Time;
		}
		afterOf( t, s );
		t.Current.push_back( move(s) );
	}
	//The end of the pass:  every boundary is settled, and the closing bound, the first record after the range, is
	//after each still without its After; past a Bad one the first non-Bad is probed for.
	Ω finish( Track& t, Prober& prober )ε->void{
		advance( t, nullopt, prober );
		if( t.Closing )
			afterOf( t, *t.Closing );
		//Past a Bad closing bound, or a last record at the range's end that served as the bound, what follows is
		//unknown:  probed for while a boundary still needs it.
		let unknown = t.Closing ? t.Closing->Class==EClass::Bad : !t.ClosingKnown && t.LastSeen.has_value();
		if( !unknown || t.Resolved==t.Settled )
			return;
		auto ahead = prober.Forward( t.Index, t.Closing ? t.Closing->Time : *t.LastSeen, t.Closing.has_value() );
		if( !t.ClosingKnown )
			t.AnyAfter = ahead.First.has_value();
		if( ahead.First )
			afterOf( t, *ahead.First );
		if( ahead.NonBad && ahead.First->Class==EClass::Bad )
			afterOf( t, *ahead.NonBad );
	}

	//One pass over the nodes' raw records in [a, b], read as Read reads them with the bounds, feeding each node's
	//track, and take with each record inside the range by the node's slot.  Each node is named once:  Read gives a
	//node named twice its bounds twice.
	Ω pass( Group& group, uint readLimit, const vector<NodeIndex>& nodes, Ticks a, Ticks b, vector<Track>& tracks, Prober& prober, absl::FunctionRef<void( uint, const Seen& )> take, SL sl )ε->void{
		absl::flat_hash_map<NodeIndex,uint> slots;
		for( uint i=0; i<nodes.size(); ++i )
			slots.try_emplace( nodes[i], i );
		vector<bool> opened( nodes.size() );//the node's opening bound, or a record at a, has come:  a bound after is its closing.
		ReadRequest request{ .Nodes=nodes, .Start=a, .End=b, .Bounds=true, .Limit=readLimit };
		for( ;; ){
			auto page = group.Read( request, sl );
			for( auto& v : page.Values ){
				let slot = slots.at( v.Value.node_index() );
				auto& t = tracks[slot];
				let time = PrimaryTime( v.Value ).value_or( a );
				let kind = classify( v.Value.status(), prober.TreatUncertainAsBad );
				Seen s{ time, move(v.Value), kind };
				let notFound = v.Bound && !s.Value.has_value() && s.Value.status()==UA_STATUSCODE_BADBOUNDNOTFOUND && (time==a || time==b);
				if( notFound || (v.Bound && time<a) ){
					if( !opened[slot] ){
						opened[slot] = true;
						if( notFound )
							t.AnyBefore = false;
						else
							open( t, move(s), prober );
					}
					else{
						t.ClosingKnown = true;
						t.AnyAfter = !notFound;
						if( !notFound )
							t.Closing = move( s );
					}
					continue;
				}
				if( v.Bound && time>b ){
					t.ClosingKnown = true;
					t.AnyAfter = true;
					t.Closing = move( s );
					continue;
				}
				if( !opened[slot] ){//a record at the range's start is its own bound:  what lies before it is unknown.
					opened[slot] = true;
					t.BeforeUnknown = true;
				}
				take( slot, s );
				feed( t, move(s), prober );
			}
			if( page.Continuation.empty() )
				break;
			request.Continuation = move( page.Continuation );
		}
		for( auto& t : tracks ){
			if( !t.ClosingKnown && !(t.LastSeen && *t.LastSeen==b) )
				t.AnyAfter = false;
			finish( t, prober );
		}
	}

	//A bounding value, or the value at a requested time:  the raw record itself, or one computed, Status
	//Good, Uncertain_DataSubNormal or Bad_NoData.
	struct Bound{ Proto::DataValue Value; StatusCode Status{ Good }; bool Raw{}; bool Extrapolated{}; };
	Ω valueOf( const Seen& s )ι->Proto::DataValue{
		Proto::DataValue y;
		if( s.Value.has_value() )
			*y.mutable_value() = s.Value.value();
		return y;
	}
	Ω computed( double d, const Seen& like )ι->Proto::DataValue{
		Proto::DataValue y;
		*y.mutable_value() = numberAs( d, like.Value.value() );
		return y;
	}
	Ω uncertain( const Bound& b )ι->bool{ return !b.Raw ? b.Status!=Good : UA_StatusCode_isUncertain( b.Value.status() ); }
	Ω missing( const Bound& b )ι->bool{ return b.Status==NoData; }
	//Part 13 §3.1.8, the interpolated bounding value at b.Time:  from the nearest non-Bad records, Uncertain where a
	//Bad one was skipped or an Uncertain one used, extrapolated from the records before when none follows.  A
	//non-numeric value, a Boolean's say, is stepped whatever the node says.
	Ω interpolated( Boundary& b, bool stepped, bool slopedExtrapolation, absl::FunctionRef<void( Boundary& )> findBefore2 )ε->Bound{
		if( b.At )
			return { b.At->Value, b.At->Value.status(), true };
		if( !b.Before )
			return { {}, NoData };
		let& before = *b.Before;
		bool unsure = b.BadBetweenBefore || before.Class==EClass::Uncertain;
		let v0 = number( before.Value );
		if( stepped || !v0 ){
			let extrapolated = !b.AnyAfter;
			return { valueOf(before), extrapolated || unsure ? SubNormal : Good, false, extrapolated };
		}
		if( !b.After ){//past the end of the data:  the line through the two before, when asked for and there are two.
			if( slopedExtrapolation ){
				if( b.Before2Unknown )
					findBefore2( b );
				if( b.Before2 && b.Before2->Time<before.Time ){
					if( let v1 = number(b.Before2->Value) )
						return { computed( *v0+(*v0-*v1)*(double)(b.Time-before.Time)/(double)(before.Time-b.Before2->Time), before ), SubNormal, false, true };
				}
			}
			return { valueOf(before), SubNormal, false, true };
		}
		let& after = *b.After;
		unsure = unsure || b.BadAt || b.BadBetweenAfter || after.Class==EClass::Uncertain;
		let v1 = number( after.Value );
		if( !v1 || after.Time<=before.Time )
			return { valueOf(before), unsure ? SubNormal : Good };
		return { computed( *v0+(*v1-*v0)*(double)(b.Time-before.Time)/(double)(after.Time-before.Time), before ), unsure ? SubNormal : Good };
	}
	//Part 13 §3.1.9, the simple bounding value:  from the nearest records whatever their status, Bad_NoData past a Bad
	//one before, stepped back to the one before past a Bad one after.
	Ω simple( const Boundary& b, bool stepped )ι->Bound{
		if( b.AtAny ){
			if( b.AtAny->Class==EClass::Bad )
				return { {}, NoData };
			return { b.AtAny->Value, b.AtAny->Value.status(), true };
		}
		if( !b.BeforeAny || b.BeforeAny->Class==EClass::Bad )
			return { {}, NoData };
		let& before = *b.BeforeAny;
		bool unsure = before.Class==EClass::Uncertain;
		let v0 = number( before.Value );
		if( stepped || !v0 ){
			let extrapolated = !b.AnyAfter;
			return { valueOf(before), extrapolated || unsure ? SubNormal : Good, false, extrapolated };
		}
		if( !b.AfterAny )
			return { valueOf(before), SubNormal, false, true };
		let& after = *b.AfterAny;
		if( after.Class==EClass::Bad )
			return { valueOf(before), SubNormal };
		unsure = unsure || after.Class==EClass::Uncertain;
		let v1 = number( after.Value );
		if( !v1 || after.Time<=before.Time )
			return { valueOf(before), unsure ? SubNormal : Good };
		return { computed( *v0+(*v1-*v0)*(double)(b.Time-before.Time)/(double)(after.Time-before.Time), before ), unsure ? SubNormal : Good };
	}
	//A result value:  a raw record as it is, bits added, or a computed one at ts.  Neither carries a heartbeat.
	Ω result( NodeIndex index, Ticks ts, Bound&& b, StatusCode bits )ι->Proto::DataValue{
		Proto::DataValue y{ move(b.Value) };
		y.set_node_index( index );
		y.clear_heartbeat();
		y.clear_heartbeat_unsourced();
		if( b.Raw ){
			y.set_status( withBits(y.status(), bits & (Partial | MultiValue)) );
			return y;
		}
		y.clear_source_ts();
		y.clear_source_picoseconds();
		y.clear_server_ts();
		y.clear_server_picoseconds();
		y.set_source_ts( ts );
		y.set_status( withBits(b.Status, missing(b) ? 0 : bits) );
		return y;
	}
	Ω noData( NodeIndex index, Ticks ts )ι->Proto::DataValue{ return result( index, ts, {{}, NoData}, 0 ); }
	Ω invalidInputs( NodeIndex index, Ticks ts )ι->Proto::DataValue{ return result( index, ts, {{}, InvalidInputs}, 0 ); }

	//The read's page as its request and continuation set it out:  what the pages before returned of the first time or
	//interval, where to resume, and the CRC that holds a continuation to its read.
	struct Paging final{
		Paging( EMode mode, const vector<NodeIndex>& nodes, sv continuation, uint32_t crc, uint limit, uint readLimit, uint64_t count, SL sl )ε:
			Nodes{ nodes.size() }, Limit{ limit && limit<readLimit ? limit : readLimit }, Crc{ crc }{
			THROW_IFSL( nodes.empty(), "A read names no nodes." );
			Had.assign( Nodes, 0 );
			if( continuation.empty() )
				return;
			Proto::Continuation from;
			if( !from.ParseFromString(string{continuation}) || from.counts_size()!=(int)Nodes || from.next()>=count )
				throw UAException{ UA_STATUSCODE_BADCONTINUATIONPOINTINVALID, "The continuation isn't one of the historian's.", {ELogLevel::Debug}, sl };
			if( from.crc()!=Crc )
				throw UAException{ UA_STATUSCODE_BADCONTINUATIONPOINTINVALID, Ƒ("The continuation is for a {} read with other arguments.", mode==EMode::AtTime ? "an at-time" : "a processed"), {ELogLevel::Debug}, sl };
			Next = from.next();
			for( uint i=0; i<Nodes; ++i )
				Had[i] = from.counts(i)!=0;
		}
		//The last time or interval the page reaches, from Next:  where its Limit values end, or the last of count.
		α Last( uint64_t count )Ι->uint64_t{
			uint64_t values{};
			for( uint64_t i=Next; i<count; ++i ){
				values += Nodes-( i==Next ? (uint)std::ranges::count(Had, true) : 0u );
				if( values>=Limit )
					return i;
			}
			return count-1;
		}
		//Emits the values of [Next, last] in order, the first's nodes but those had, until the page is full, with the
		//continuation at the cut.  value gives the node's value at i by slot.
		α Emit( uint64_t count, absl::FunctionRef<Proto::DataValue( uint64_t, uint )> value )ε->ReadResult{
			ReadResult y;
			uint emitted{};
			for( uint64_t i=Next; i<count; ++i ){
				vector<bool> done( Nodes );
				for( uint slot=0; slot<Nodes; ++slot ){
					if( i==Next && Had[slot] ){
						done[slot] = true;
						continue;
					}
					if( emitted==Limit ){
						Proto::Continuation next;
						next.set_crc( Crc );
						next.set_next( i );
						for( uint j=0; j<Nodes; ++j )
							next.add_counts( done[j] ? 1 : 0 );
						y.Continuation = next.SerializeAsString();
						return y;
					}
					y.Values.push_back( {value(i, slot), false, {}} );
					done[slot] = true;
					++emitted;
				}
			}
			return y;
		}
		uint Nodes;
		uint Limit;
		uint32_t Crc;
		uint64_t Next{};
		vector<bool> Had;
	};
	//The request's nodes each once, which the pass reads and the values are of, and each requested slot's place among
	//them:  a node named twice is read once and answered twice.
	struct Distinct final{
		Distinct( const vector<NodeIndex>& requested )ι{
			absl::flat_hash_map<NodeIndex,uint> at;
			for( let index : requested ){
				let [p, added] = at.try_emplace( index, (uint)Nodes.size() );
				if( added )
					Nodes.push_back( index );
				Of.push_back( p->second );
			}
		}
		vector<NodeIndex> Nodes;
		vector<uint> Of;
	};
	Ω configuration( CodedOutputStream& c, const AggregateConfiguration& config )ι->void{
		c.WriteVarint32( config.TreatUncertainAsBad ? 1 : 0 );
		c.WriteVarint32( config.PercentDataBad );
		c.WriteVarint32( config.PercentDataGood );
		c.WriteVarint32( config.UseSlopedExtrapolation ? 1 : 0 );
	}
	//The CRC-32C of a read's arguments but Limit, the mode first, so a continuation resumes only its own read.
	Ω crc( const AtTimeRequest& r )ι->uint32_t{
		string bytes;
		{
			google::protobuf::io::StringOutputStream out{ &bytes };
			CodedOutputStream c{ &out };
			c.WriteVarint32( (uint32_t)EMode::AtTime );
			c.WriteVarint64( r.Nodes.size() );
			for( let node : r.Nodes )
				c.WriteVarint32( node );
			c.WriteVarint64( r.Times.size() );
			for( let t : r.Times )
				c.WriteLittleEndian64( (uint64_t)t );
			c.WriteVarint32( r.SimpleBounds ? 1 : 0 );
			configuration( c, r.Configuration );
		}
		return IO::Crc::Calc32c( bytes );
	}
	Ω crc( const ProcessedRequest& r )ι->uint32_t{
		string bytes;
		{
			google::protobuf::io::StringOutputStream out{ &bytes };
			CodedOutputStream c{ &out };
			c.WriteVarint32( (uint32_t)EMode::Processed );
			c.WriteVarint64( r.Nodes.size() );
			for( let node : r.Nodes )
				c.WriteVarint32( node );
			c.WriteLittleEndian64( (uint64_t)r.Start );
			c.WriteLittleEndian64( (uint64_t)r.End );
			c.WriteLittleEndian64( (uint64_t)ticks(r.Interval) );
			c.WriteVarint32( (uint32_t)r.Aggregate );
			configuration( c, r.Configuration );
		}
		return IO::Crc::Calc32c( bytes );
	}
	Ω validate( const AggregateConfiguration& c, SL sl )ε->void{
		if( c.PercentDataBad>100 || c.PercentDataGood>100 || c.PercentDataGood<100-c.PercentDataBad )
			throw UAException{ UA_STATUSCODE_BADAGGREGATEINVALIDINPUTS, Ƒ("PercentDataBad {} and PercentDataGood {} aren't a valid pair.", c.PercentDataBad, c.PercentDataGood), {ELogLevel::Debug}, sl };
	}
	//Part 13 §5.4.3.2's calculation by counts:  Bad at PercentDataBad of the values, else Good at PercentDataGood,
	//else Uncertain; with the two complementary the Good test comes first, as §4.2.1.2 says.
	Ω byCounts( uint good, uint bad, uint total, const AggregateConfiguration& c )ι->StatusCode{
		let goodEnough = good*100>=c.PercentDataGood*total, badEnough = bad*100>=c.PercentDataBad*total;
		if( c.PercentDataGood==100-c.PercentDataBad )
			return goodEnough ? Good : badEnough ? Bad : SubNormal;
		return badEnough ? Bad : goodEnough ? Good : SubNormal;
	}

	//An interval's raw records of one node, as its aggregate needs them.
	struct Bucket final{
		uint Count{}, GoodCount{}, BadCount{}, UncertainCount{};
		bool NonNumeric{};//a non-Bad value without a number, which the numeric aggregates can't take.
		double Sum{}, Mean{}, M2{};//Welford's, over the Good values.
		vector<double> Values;//Median's Good values.
		vector<std::pair<Ticks,double>> Points;//TimeAverage's non-Bad values, in time order.
		optional<Seen> Min, Max;//Minimum's and Maximum's alone, since a copy allocates the value.
		double MinValue{}, MaxValue{};
		uint MinCount{}, MaxCount{};
		optional<Seen> First, Last;//of any status:  Start's and End's alone.
		α Take( const Seen& s, EAggregate aggregate )ι->void{
			++Count;
			if( aggregate==EAggregate::Start && !First )
				First = s;
			if( aggregate==EAggregate::End )
				Last = s;
			if( s.Class==EClass::Bad ){
				++BadCount;
				return;
			}
			let v = number( s.Value );
			if( aggregate==EAggregate::TimeAverage ){
				if( v )
					Points.emplace_back( s.Time, *v );
				else
					NonNumeric = true;
			}
			if( s.Class==EClass::Uncertain ){
				++UncertainCount;
				return;
			}
			++GoodCount;
			if( aggregate==EAggregate::Count || aggregate==EAggregate::Start || aggregate==EAggregate::End || aggregate==EAggregate::Interpolative || aggregate==EAggregate::TimeAverage )
				return;
			if( !v ){
				NonNumeric = true;
				return;
			}
			Sum += *v;
			let delta = *v-Mean;
			Mean += delta/GoodCount;
			M2 += delta*( *v-Mean );
			if( aggregate==EAggregate::Median )
				Values.push_back( *v );
			else if( aggregate==EAggregate::Minimum ){
				if( !Min || *v<MinValue ){
					Min = s;
					MinValue = *v;
					MinCount = 1;
				}
				else if( *v==MinValue )
					++MinCount;
			}
			else if( aggregate==EAggregate::Maximum ){
				if( !Max || *v>MaxValue ){
					Max = s;
					MaxValue = *v;
					MaxCount = 1;
				}
				else if( *v==MaxValue )
					++MaxCount;
			}
		}
	};
	//One interval of a processed read:  [Lo, Hi) stamped Lo, or in a reversed read (Lo, Hi] stamped Hi.
	struct Interval final{ Ticks Lo, Hi, Ts; bool Reverse; bool Uneven; };
	//The aggregate of an interval for one node, from its bucket, the node's track, and the boundaries at the
	//interval's ends where the aggregate uses bounds (Part 13 §5.4.3).
	struct Finishing final{
		const ProcessedRequest& Request;
		Track& T;
		bool Stepped;
		Prober& Probe;
		//Whether the node has a record before the interval, and one after it:  probed for when the pass couldn't tell.
		α DataBefore( const Interval& iv )ε->bool{
			if( T.FirstSeen && (*T.FirstSeen<iv.Lo || (iv.Reverse && *T.FirstSeen==iv.Lo)) )
				return true;
			if( !T.AnyBefore )
				T.AnyBefore = T.FirstSeen && Probe.Exists( T.Index, *T.FirstSeen, true );
			return *T.AnyBefore;
		}
		α DataAfter( const Interval& iv )ε->bool{
			if( T.LastSeen && (*T.LastSeen>iv.Hi || (!iv.Reverse && *T.LastSeen==iv.Hi)) )
				return true;
			if( !T.AnyAfter )
				T.AnyAfter = T.LastSeen && Probe.Exists( T.Index, *T.LastSeen, false );
			return *T.AnyAfter;
		}
		//Part 13 §5.3.3.2:  the interval overlaps the beginning or the end of the data, or is the uneven last one.
		α IsPartial( const Interval& iv, const Bucket& b )ε->bool{ return b.Count && ( !DataBefore(iv) || !DataAfter(iv) || iv.Uneven ); }
		α BoundAt( Ticks time, bool stepped )ε->Bound{
			auto p = std::ranges::lower_bound( T.Boundaries, time, {}, &Boundary::Time );
			ASSERT( p!=T.Boundaries.end() && p->Time==time );
			return interpolated( *p, stepped, Request.Configuration.UseSlopedExtrapolation, [this]( Boundary& b ){
				auto behind = Probe.Back( T.Index, b.Before->Time, 1, true, true );
				if( !behind.Found.empty() )
					b.Before2 = move( behind.Found[0] );
				b.Before2Unknown = false;
			});
		}
		α Finish( const Interval& iv, Bucket& b )ε->Proto::DataValue{
			let index = T.Index;
			//Asked only by the cases that set the bit, since asking can probe:  Average's and Interpolative's tables leave it
			//unset, and TimeAverage has its own rule.
			let partial = [&]()->StatusCode{ return IsPartial( iv, b ) ? Partial : 0; };
			let& config = Request.Configuration;
			switch( Request.Aggregate ){
			case EAggregate::Interpolative:
				return result( index, iv.Ts, BoundAt(iv.Ts, Stepped), Interpolated );
			case EAggregate::Count:{
				if( !b.Count )
					return noData( index, iv.Ts );
				let status = byCounts( b.GoodCount, b.BadCount, b.Count, config );
				if( status==Bad )
					return result( index, iv.Ts, {{}, Bad}, 0 );
				Proto::DataValue y;
				y.mutable_value()->set_int32( (int32_t)b.GoodCount );
				return result( index, iv.Ts, {move(y), status}, Calculated | partial() );}
			case EAggregate::Average:{
				if( !b.GoodCount )
					return noData( index, iv.Ts );
				if( b.NonNumeric )
					return invalidInputs( index, iv.Ts );
				let status = byCounts( b.GoodCount, b.BadCount, b.Count, config );
				if( status==Bad )
					return result( index, iv.Ts, {{}, Bad}, 0 );
				Proto::DataValue y;
				y.mutable_value()->set_double_value( b.Sum/b.GoodCount );
				return result( index, iv.Ts, {move(y), status}, Calculated );}
			case EAggregate::Minimum:
			case EAggregate::Maximum:{
				if( !b.GoodCount )
					return noData( index, iv.Ts );
				if( b.NonNumeric )
					return invalidInputs( index, iv.Ts );
				let& extreme = Request.Aggregate==EAggregate::Minimum ? *b.Min : *b.Max;
				let count = Request.Aggregate==EAggregate::Minimum ? b.MinCount : b.MaxCount;
				let status = b.BadCount ? SubNormal : Good;
				let raw = status==Good && extreme.Time==iv.Ts;//the record itself, as Start's is.
				return result( index, iv.Ts, raw ? Bound{extreme.Value, extreme.Value.status(), true} : Bound{valueOf(extreme), status}, (raw ? 0 : Calculated) | (count>1 ? MultiValue : 0) | partial() );}
			case EAggregate::Start:
			case EAggregate::End:{
				if( !b.Count )
					return noData( index, iv.Ts );
				let& raw = Request.Aggregate==EAggregate::Start ? *b.First : *b.Last;
				return result( index, iv.Ts, {raw.Value, raw.Value.status(), true}, partial() );}
			case EAggregate::StandardDeviationSample:
			case EAggregate::Median:{
				if( !b.GoodCount )
					return noData( index, iv.Ts );
				if( b.NonNumeric )
					return invalidInputs( index, iv.Ts );
				Proto::DataValue y;
				if( Request.Aggregate==EAggregate::Median ){
					auto& values = b.Values;
					let mid = values.begin()+values.size()/2;
					std::ranges::nth_element( values, mid );
					double median = *mid;
					if( values.size()%2==0 )
						median = ( median+*std::ranges::max_element(values.begin(), mid) )/2;
					y.mutable_value()->set_double_value( median );
				}
				else
					y.mutable_value()->set_double_value( b.GoodCount==1 ? 0 : std::sqrt(b.M2/(b.GoodCount-1)) );
				return result( index, iv.Ts, {move(y), b.Count>b.GoodCount ? SubNormal : Good}, Calculated | partial() );}
			case EAggregate::TimeAverage:
				return TimeAverage( iv, b );
			}
			return noData( index, iv.Ts );
		}
		//Part 13 §5.4.3.6:  the area under the line through the start bound, the non-Bad values inside and the end
		//bound, sloped whatever the node says, over the interval's width, or from its first value when the start bound
		//is missing, a partial interval.  Uncertain where a bound is, a Bad value was skipped or an Uncertain one used.
		α TimeAverage( const Interval& iv, Bucket& b )ε->Proto::DataValue{
			let index = T.Index;
			if( b.NonNumeric )
				return invalidInputs( index, iv.Ts );
			auto start = BoundAt( iv.Lo, false ), end = BoundAt( iv.Hi, false );
			if( missing(start) && b.Points.empty() )
				return noData( index, iv.Ts );//before the start of the data.
			if( b.Points.empty() && end.Extrapolated )
				return noData( index, iv.Ts );//after its end.
			let startValue = missing(start) ? nullopt : number( start.Value ), endValue = number( end.Value );
			if( (!missing(start) && !startValue) || !endValue )
				return invalidInputs( index, iv.Ts );
			vector<std::pair<Ticks,double>> line;
			if( startValue )
				line.emplace_back( iv.Lo, *startValue );
			for( let& p : b.Points )
				line.push_back( p );
			line.emplace_back( iv.Hi, *endValue );
			double area{};
			for( uint i=1; i<line.size(); ++i )
				area += ( line[i-1].second+line[i].second )/2*(double)( line[i].first-line[i-1].first );
			let from = line.front().first;
			Proto::DataValue y;
			y.mutable_value()->set_double_value( iv.Hi>from ? area/(double)(iv.Hi-from) : line.front().second );
			let unsure = missing(start) || uncertain(start) || uncertain(end) || b.BadCount || b.UncertainCount;
			let partial = missing(start) || end.Extrapolated || iv.Uneven ? Partial : 0;
			return result( index, iv.Ts, {move(y), unsure ? SubNormal : Good}, Calculated | partial );
		}
	};

	α Group::ReadAtTime( const AtTimeRequest& r, SL sl )ε->ReadResult{
		THROW_IFSL( r.Times.empty(), "An at-time read names no times." );
		let readLimit = _store->Config.ReadLimit;
		Paging paging{ EMode::AtTime, r.Nodes, r.Continuation, crc(r), r.Limit, readLimit, r.Times.size(), sl };
		let last = paging.Last( r.Times.size() );
		//The page's times, each once, in segments by day:  a segment's pass reads the day's records between its times and
		//the bounds either side, not the days between segments.
		vector<Ticks> times{ r.Times.begin()+paging.Next, r.Times.begin()+last+1 };
		std::ranges::sort( times );
		times.erase( std::ranges::unique(times).begin(), times.end() );
		let& tz = *_store->Config.TimeZone;
		Prober prober{ *this, readLimit, r.Configuration.TreatUncertainAsBad, sl };
		const Distinct nodes{ r.Nodes };
		vector<bool> stepped;
		for( let index : nodes.Nodes )
			stepped.push_back( FindThresholds(index).value_or(Thresholds{}).Stepped );
		absl::flat_hash_map<Ticks,vector<Proto::DataValue>> byTime;
		for( uint from=0; from<times.size(); ){
			uint to = from+1;
			for( let day = DayOf(times[from], tz); to<times.size() && DayOf(times[to], tz)==day; ++to )
			{}
			vector<Track> tracks( nodes.Nodes.size() );
			for( uint slot=0; slot<tracks.size(); ++slot ){
				tracks[slot].Index = nodes.Nodes[slot];
				for( uint i=from; i<to; ++i )
					tracks[slot].Boundaries.push_back( {.Time=times[i]} );
			}
			pass( *this, readLimit, nodes.Nodes, times[from], times[to-1], tracks, prober, []( uint, const Seen& ){}, sl );
			for( uint slot=0; slot<tracks.size(); ++slot ){
				auto& t = tracks[slot];
				for( auto& b : t.Boundaries ){
					auto bound = r.SimpleBounds ? simple( b, stepped[slot] ) : interpolated( b, stepped[slot], r.Configuration.UseSlopedExtrapolation, [&]( Boundary& x ){
						auto behind = prober.Back( t.Index, x.Before->Time, 1, true, true );
						if( !behind.Found.empty() )
							x.Before2 = move( behind.Found[0] );
						x.Before2Unknown = false;
					});
					auto& at = byTime[b.Time];
					at.resize( nodes.Nodes.size() );
					at[slot] = result( t.Index, b.Time, move(bound), Interpolated );
				}
			}
			from = to;
		}
		return paging.Emit( r.Times.size(), [&]( uint64_t i, uint slot ){ return byTime.at( r.Times[i] )[nodes.Of[slot]]; } );
	}

	α Group::ReadProcessed( const ProcessedRequest& r, SL sl )ε->ReadResult{
		THROW_IFSL( r.Nodes.empty(), "A read names no nodes." );
		if( r.Start==r.End )
			throw UAException{ UA_STATUSCODE_BADINVALIDARGUMENT, "A processed read's start is its end.", {ELogLevel::Debug}, sl };
		THROW_IFSL( r.Interval<Duration::zero(), "A processed read's interval is negative." );
		validate( r.Configuration, sl );
		let readLimit = _store->Config.ReadLimit;
		let reverse = r.Start>r.End;
		let range = reverse ? ticksBetween( r.End, r.Start ) : ticksBetween( r.Start, r.End );
		let interval = ticks( r.Interval );
		let width = interval<=0 || (uint64_t)interval>=range ? range : (uint64_t)interval;
		let count = range/width + ( range%width ? 1 : 0 );
		Paging paging{ EMode::Processed, r.Nodes, r.Continuation, crc(r), r.Limit, readLimit, count, sl };
		let last = paging.Last( count );
		let intervalOf = [&]( uint64_t k ){
			Interval y{ .Reverse=reverse, .Uneven=k+1==count && range%width!=0 };
			let offset = k*width, rest = range-offset;//from the range's start to the interval's, and on to the range's end.
			if( reverse ){
				y.Hi = (Ticks)( (uint64_t)r.Start-offset );
				y.Lo = rest>width ? (Ticks)( (uint64_t)y.Hi-width ) : r.End;
				y.Ts = y.Hi;
			}
			else{
				y.Lo = (Ticks)( (uint64_t)r.Start+offset );
				y.Hi = rest>width ? (Ticks)( (uint64_t)y.Lo+width ) : r.End;
				y.Ts = y.Lo;
			}
			return y;
		};
		let first = intervalOf( paging.Next ), lastInterval = intervalOf( last );
		let a = std::min( first.Lo, lastInterval.Lo ), b = std::max( first.Hi, lastInterval.Hi );
		let bounds = r.Aggregate==EAggregate::Interpolative || r.Aggregate==EAggregate::TimeAverage;
		Prober prober{ *this, readLimit, r.Configuration.TreatUncertainAsBad, sl };
		const Distinct nodes{ r.Nodes };
		vector<Track> tracks( nodes.Nodes.size() );
		vector<vector<Bucket>> buckets( nodes.Nodes.size() );
		for( uint slot=0; slot<tracks.size(); ++slot ){
			tracks[slot].Index = nodes.Nodes[slot];
			buckets[slot].resize( last-paging.Next+1 );
			if( !bounds )
				continue;
			for( uint64_t k=paging.Next; k<=last; ++k ){
				let iv = intervalOf( k );
				if( r.Aggregate==EAggregate::TimeAverage ){
					tracks[slot].Boundaries.push_back( {.Time=iv.Lo} );
					tracks[slot].Boundaries.push_back( {.Time=iv.Hi} );
				}
				else
					tracks[slot].Boundaries.push_back( {.Time=iv.Ts} );
			}
			auto& boundaries = tracks[slot].Boundaries;
			std::ranges::sort( boundaries, {}, &Boundary::Time );
			boundaries.erase( std::ranges::unique(boundaries, {}, &Boundary::Time).begin(), boundaries.end() );
		}
		//A record's interval by its time:  none for one at the range's far end, which is a bound alone.
		let slotOf = [&]( Ticks t )->optional<uint64_t>{
			let k = reverse ? ( t>r.Start || t<=r.End ? count : ticksBetween(t, r.Start)/width ) : ( t<r.Start || t>=r.End ? count : ticksBetween(r.Start, t)/width );
			return k>=paging.Next && k<=last ? optional<uint64_t>{ k-paging.Next } : nullopt;
		};
		pass( *this, readLimit, nodes.Nodes, a, b, tracks, prober, [&]( uint slot, const Seen& s ){
			if( let k = slotOf(s.Time) )
				buckets[slot][*k].Take( s, r.Aggregate );
		}, sl );
		vector<Finishing> finishing;
		for( uint slot=0; slot<tracks.size(); ++slot )
			finishing.push_back( {r, tracks[slot], FindThresholds(nodes.Nodes[slot]).value_or(Thresholds{}).Stepped, prober} );
		return paging.Emit( count, [&]( uint64_t k, uint slot ){
			let node = nodes.Of[slot];
			return finishing[node].Finish( intervalOf(k), buckets[node][k-paging.Next] );
		});
	}
}