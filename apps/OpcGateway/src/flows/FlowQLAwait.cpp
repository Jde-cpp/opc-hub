#include "FlowQLAwait.h"
#include <charconv>
#include <jde/fwk/io/crc.h>
#include <jde/access/Authorize.h>
#include <jde/opc/uatypes/DateTime.h>
#include "../async/BlockingPool.h"
#include "../GatewayAppClient.h"
#include "FlowHost.h"

#define let const auto
namespace Jde::Opc::Gateway{
	using Flows::TraceRecord;
	Ω invalid( sv command, string what, SL sl )ι->Exception{ return Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "{}:  {}", command, what }; }
	//A time argument, none for one absent or null.
	Ω time( const QL::Input& input, sv name, sv command, SL sl )ε->optional<TimePoint>{
		let p = input.FindPtr<jvalue>( name );
		if( !p || p->is_null() )
			return nullopt;
		try{
			return UADateTime{ *p, sl }.Time();
		}
		catch( const Exception& e ){
			throw invalid( command, Ƒ("'{}' is not a time:  {}", name, e.what()), sl );
		}
	}
	//A message's id:  a Long, a number, or its decimal text.
	Ω id( const jvalue& v, sv name, sv command, SL sl )ε->Flows::MessageId{
		try{
			if( v.is_string() ){
				let& s = v.get_string();
				Flows::MessageId y{};
				let [end, error] = std::from_chars( s.data(), s.data()+s.size(), y );
				if( error!=std::errc{} || end!=s.data()+s.size() )
					throw invalid( command, Ƒ("'{}' is not an id:  {}", name, serialize(v)), sl );
				return y;
			}
			return Json::AsNumber<uint64_t>( v.is_object() ? FromLong(v.get_object(), sl) : v, sl );
		}
		catch( const Exception& e ){
			throw invalid( command, Ƒ("'{}' is not an id:  {}", name, e.what()), sl );
		}
	}
	//A page's limit:  the caller's, at most the block's readLimit, which 0 lifts.
	Ω limit( const QL::Input& input, uint readLimit )ι->uint{
		let most = readLimit ? readLimit : std::numeric_limits<uint>::max();
		let asked = input.TryNumber<uint>( "limit" ).value_or( 0 );
		return asked && asked<most ? asked : most;
	}
	//The continuation:  the crc of the read's other arguments, which another read's is refused by, then the cursor's time
	//and skip, little-endian, as base64url.
	Ω encode( uint32_t crc, const Flows::TraceCursor& cursor )ι->string{
		string bytes;
		let put = [&]( uint64_t v, uint size ){
			for( uint i=0; i<size; ++i )
				bytes += (char)( v>>(i*8) );
		};
		put( crc, 4 );
		put( (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(cursor.Time.time_since_epoch()).count(), 8 );
		put( cursor.Skip, 4 );
		return Str::Encode64( bytes, true );
	}
	Ω decode( sv command, sv text, uint32_t crc, SL sl )ε->Flows::TraceCursor{
		string bytes;
		try{
			bytes = Str::Decode64( text, true, sl );
		}
		catch( const Exception& ){
			throw invalid( command, "the continuation isn't one of the gateway's", sl );
		}
		if( bytes.size()!=16 )
			throw invalid( command, "the continuation isn't one of the gateway's", sl );
		let get = [&]( uint offset, uint size ){
			uint64_t y{};
			for( uint i=0; i<size; ++i )
				y |= (uint64_t)(uint8_t)bytes[offset+i]<<(i*8);
			return y;
		};
		if( get(0, 4)!=crc )
			throw invalid( command, "the continuation is for a read with other arguments", sl );
		return { TimePoint{ std::chrono::duration_cast<Duration>(std::chrono::nanoseconds{(int64_t)get(4, 8)}) }, (uint32)get(12, 4) };
	}
	//The trace a read is of:  the live one with no `run`, or a replay's.
	Ω run( const QL::Input& input, SL sl )ε->uint32{
		let p = input.FindPtr<jvalue>( "run" );
		return p && !p->is_null() ? input.AsNumber<uint32>( "run", sl ) : 0;
	}
	Ω idJson( Flows::MessageId id )ι->jvalue{ return id ? jvalue{ Flows::ToLong(id) } : jvalue{}; }
	//A record as the result carries it:  its json parsed, with the web's Longs, or its cut text.
	Ω recordJson( const TraceRecord& r )ι->jobject{
		jobject y{ {"time", ToIsoString(r.Time)}, {"msgId", idJson(r.Id)}, {"parentId", idJson(r.Parent)}, {"runId", r.Run}, {"node", r.Node}, {"kind", ToString(r.Kind)}, {"cut", r.Cut} };
		jarray ports;
		for( let port : r.Ports )
			ports.push_back( port );
		y["ports"] = move( ports );
		jvalue json;
		if( r.Json && !r.Cut ){
			try{
				json = Json::ParseValue( *r.Json );
				Flows::ToWire( json );
			}
			catch( const std::exception& ){
				json = *r.Json;
			}
		}
		else if( r.Json )
			json = *r.Json;
		y["json"] = move( json );
		y["error"] = r.Error.empty() ? jvalue{} : jvalue{ r.Error };
		y["status"] = r.Status ? jvalue{ jobject{{"fill", r.Status->Fill}, {"shape", r.Status->Shape}, {"text", r.Status->Text}} } : jvalue{};
		jarray blocks;
		for( let& block : r.Blocks )
			blocks.push_back( jvalue{block} );
		y["blocks"] = move( blocks );
		return y;
	}
	Ω recordsJson( const vector<TraceRecord>& records )ι->jarray{
		jarray y;
		y.reserve( records.size() );
		for( let& r : records )
			y.push_back( recordJson(r) );
		return y;
	}

	α FlowQLAwait::Query()ι->TAwait<jvalue>::Task{
		try{
			let [flow, store] = Gate( _sl );
			//On the trace reads' pool:  a range of day files decoded, which an executor thread is not to wait on.
			auto y = co_await FlowReadAwait<jobject>{ [this, flow, store]{
				if( _query.JsonName==TraceCommand )
					return Trace( flow, *store, _sl );
				if( _query.JsonName==MessageCommand )
					return Message( flow, *store, _sl );
				return Counts( flow, *store, _sl );
			}, _sl };
			Resume( _query.TrimColumns(y) );
		}
		catch( runtime_error& e ){
			ResumeExp( move(e) );
		}
		co_return;
	}
	α FlowQLAwait::Gate( SL sl )ε->std::pair<FlowRow,sp<Flows::TraceStore>>{
		auto& host = FlowHost::Instance();
		host.TestEnabled( sl );
		let id = _query.TryNumber<FlowPK>( "flow" );
		if( !id )
			throw invalid( _query.JsonName, "'flow' is a flows id", sl );
		let state = host.Find( *id );
		AppClient()->Acl()->Test( "gateway", "flows", state ? state->Row.Guid : string{}, Access::ERights::Read, UserPK(), sl );//the table's for a flow that isn't there, before a caller learns of an id.
		if( !state )
			throw Exception{ sl, ExceptionArgs{EHttpStatus::NotFound}, "Flow {} not found.", *id };
		auto store = host.Store();
		THROW_IFSL( !store, "The gateway's flows have stopped." );
		return { state->Row, move(store) };
	}
	α FlowQLAwait::Trace( const FlowRow& flow, Flows::TraceStore& store, SL sl )ε->jobject{
		Flows::TraceQuery query{ .Start=time(_query, "start", TraceCommand, sl), .End=time(_query, "end", TraceCommand, sl), .Limit=limit(_query, FlowHost::Instance().ReadLimit()) };
		if( let node = _query.FindPtr<jstring>("node"); node && node->size() )
			query.Node = string{ *node };
		if( let msg = _query.FindPtr<jvalue>("msg"); msg && !msg->is_null() )
			query.Msg = id( *msg, "msg", TraceCommand, sl );
		if( let kind = _query.FindPtr<jstring>("kind"); kind && kind->size() ){
			query.Kind = Flows::ToRecordKind( *kind );
			if( !query.Kind )
				throw invalid( TraceCommand, Ƒ("'kind' is hop, birth, clone, error, status, drop or captured, not '{}'", sv{*kind}), sl );
		}
		query.Run = run( _query, sl );
		if( !query.Start && !query.End )
			throw invalid( TraceCommand, "needs a start or an end", sl );
		let iso = []( optional<TimePoint> t ){ return t ? ToIsoString( *t ) : string{}; };
		let crc = IO::Crc::Calc32c( Ƒ("{}|{}|{}|{}|{}|{}|{}", flow.Id, iso(query.Start), iso(query.End), query.Node.value_or(""), query.Msg.value_or(0), query.Kind ? ToString(*query.Kind) : sv{}, query.Run) );
		if( let c = _query.FindPtr<jstring>("continuation"); c && c->size() )
			query.From = decode( TraceCommand, *c, crc, sl );
		auto page = store.Read( flow.Slug, query, sl );
		return { {"records", recordsJson(page.Records)}, {"continuation", page.Next ? jvalue{ encode(crc, *page.Next) } : jvalue{}} };
	}
	α FlowQLAwait::Message( const FlowRow& flow, Flows::TraceStore& store, SL sl )ε->jobject{
		let messageId = _query.FindPtr<jvalue>( "id" );
		if( !messageId || messageId->is_null() )
			throw invalid( MessageCommand, "needs id, the message's", sl );
		auto lineage = store.Lineage( flow.Slug, id(*messageId, "id", MessageCommand, sl), time(_query, "start", MessageCommand, sl), time(_query, "end", MessageCommand, sl), limit(_query, FlowHost::Instance().ReadLimit()), run(_query, sl), sl );
		return { {"records", recordsJson(lineage.Records)}, {"more", lineage.More} };
	}
	α FlowQLAwait::Counts( const FlowRow& flow, Flows::TraceStore& store, SL sl )ε->jobject{
		let start = time( _query, "start", CountsCommand, sl ), end = time( _query, "end", CountsCommand, sl );
		if( !start || !end )
			throw invalid( CountsCommand, "needs a start and an end", sl );
		let by = _query.FindPtr<jstring>( "by" );
		if( !by || by->empty() )
			throw invalid( CountsCommand, "needs by, a duration of a minute or more", sl );
		let width = Chrono::TryToDuration( *by );
		if( !width || *width<std::chrono::minutes{1} )
			throw invalid( CountsCommand, Ƒ("'by' is a duration of a minute or more, not '{}'", sv{*by}), sl );
		let of = run( _query, sl );
		let crc = IO::Crc::Calc32c( Ƒ("{}|{}|{}|{}|{}", flow.Id, ToIsoString(*start), ToIsoString(*end), sv{*by}, of) );
		optional<TimePoint> from;
		if( let c = _query.FindPtr<jstring>("continuation"); c && c->size() )
			from = decode( CountsCommand, *c, crc, sl ).Time;
		let page = store.Counts( flow.Slug, *start, *end, *width, limit(_query, FlowHost::Instance().ReadLimit()), from, of, sl );
		jarray counts;
		for( let& row : page.Rows ){
			jarray sent;
			for( let port : row.Counts.Sent )
				sent.push_back( port );
			counts.push_back( jobject{ {"time", ToIsoString(row.Time)}, {"node", row.Counts.Node}, {"received", row.Counts.Received}, {"sent", move(sent)}, {"drops", row.Counts.Drops}, {"errors", row.Counts.Errors} } );
		}
		return { {"counts", move(counts)}, {"continuation", page.Next ? jvalue{ encode(crc, {*page.Next, 0}) } : jvalue{}} };
	}
}