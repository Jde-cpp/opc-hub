#include "HistQL.h"
#include <bit>
#include <jde/fwk/io/crc.h>
#include <jde/fwk/settings.h>
#include <jde/fwk/str.h>
#include <jde/historian/Historian.h>
#include <jde/opc/UAException.h>
#include <jde/opc/uatypes/DateTime.h>
#include <jde/ql/types/TableQL.h>
#include "../hist/HistRows.h"

#define let const auto
namespace Jde::Opc::Gateway{
	Ω time( const jvalue* p, SL sl )ε->optional<UA_DateTime>{
		return p && !p->is_null() ? optional<UA_DateTime>{ UADateTime{*p, sl}.UA() } : nullopt;
	}
	Ω time( const QL::Input& input, sv name, SL sl )ε->optional<UA_DateTime>{
		return time( input.FindPtr<jvalue>(name), sl );
	}
	//A value's status, a number as a read answers it;  none, so Good, when absent or null.
	Ω status( const jvalue* p, sv command, SL sl )ε->optional<StatusCode>{
		if( !p || p->is_null() )
			return nullopt;
		let y = p->try_to_number<StatusCode>();
		THROW_IFSL( !y, "{}:  a value's status isn't a number - {}.", command, serialize(*p) );
		return *y;
	}
	//{ns, i|s|g|b}, a bare numeric id, or the UA spelling, "ns=1;i=6012".
	Ω node( const jvalue& j )ε->NodeId{
		return j.is_string() ? NodeId::DecodeJson( string{j.get_string()} ) : NodeId{ j };
	}
	Ω invalid( string what, SL sl )ι->UAException{
		return UAException{ UA_STATUSCODE_BADCONTINUATIONPOINTINVALID, move(what), {ELogLevel::Debug}, sl };//a caller's mistake:  said at Debug.
	}
	Ω given( const QL::Input& input, sv name )ι->bool{
		let p = input.FindPtr<jvalue>( name );
		return p && !p->is_null();
	}
	α HistQL::Mode( const QL::Input& input )ι->EMode{
		return given( input, "times" ) ? EMode::AtTime : given( input, "aggregate" ) || given( input, "interval" ) ? EMode::Processed : EMode::Raw;
	}
	α HistQL::Target( const QL::Input& input )ι->ETarget{
		let opc = input.FindPtr<jstring>( "opc" )!=nullptr, group = given( input, "group" );
		return opc==group ? ETarget::Neither : opc ? ETarget::Opc : ETarget::Group;
	}
	α HistQL::TargetRefused( sv command, SL sl )ι->Exception{
		return Exception{ sl, {}, "{} takes exactly one of 'opc' and 'group'.", command };
	}
	//A group's hist_groups id.
	Ω groupId( const QL::Input& input, sv command, SL sl )ε->uint32{
		let id = input.FindPtr<jvalue>( "group" )->try_to_number<uint32>();
		if( !id )
			throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "{}:  'group' is a hist_groups id, not {}.", command, serialize(*input.FindPtr<jvalue>("group")) };
		return *id;
	}
	//Each of `nodes`, an array or one.
	Ω eachNode( const QL::Input& input, sv command, SL sl, auto&& add )ε->void{
		let nodes = input.FindPtr<jvalue>( "nodes" );
		THROW_IFSL( !nodes || nodes->is_null(), "{} names no nodes.", command );
		if( nodes->is_array() ){
			for( let& j : nodes->get_array() )
				add( j );
		}
		else
			add( *nodes );
	}
	α HistQL::Nodes( const QL::Input& input, sv command, SL sl )ε->vector<jvalue>{
		vector<jvalue> y;
		eachNode( input, command, sl, [&]( const jvalue& j ){ y.push_back( j ); } );
		return y;
	}
namespace HistQL{
	ReadArgs::ReadArgs( const QL::Input& input, SL sl )ε:
		Mode{ HistQL::Mode(input) }{
		Start = time( input, "start", sl );
		End = time( input, "end", sl );
		Modified = input.Find<bool>( "modified" ).value_or( false );
		Bounds = input.Find<bool>( "returnBounds" ).value_or( false );
		if( Mode==EMode::AtTime ){//each mode refuses the others' arguments (spec *Reads*).
			THROW_IFSL( Start || End || Modified || Bounds || given(input, "aggregate") || given(input, "interval"), "An at-time history takes no start, end, modified, returnBounds, interval or aggregate." );
			let& times = *input.FindPtr<jvalue>( "times" );
			if( times.is_array() ){
				for( let& j : times.get_array() )
					Times.push_back( UADateTime{j, sl}.UA() );
			}
			else
				Times.push_back( UADateTime{times, sl}.UA() );
			THROW_IFSL( Times.empty(), "An at-time history names no times." );
		}
		else if( Mode==EMode::Processed ){
			THROW_IFSL( Modified || Bounds, "An aggregate history takes no modified or returnBounds." );
			THROW_IFSL( !Start || !End, "An aggregate history needs a start and an end." );
			let aggregate = input.FindPtr<jstring>( "aggregate" );
			let interval = input.TryNumber<double>( "interval" );
			THROW_IFSL( !aggregate || aggregate->empty() || !interval, "An aggregate history needs an aggregate's name and an interval in milliseconds." );
			THROW_IFSL( *interval<0, "An aggregate history's interval is negative - {}.", *interval );
			Aggregate = *aggregate;
			Interval = *interval;
		}
		else
			THROW_IFSL( !Start && !End, "history needs a start or an end." );
		let readLimit = ReadLimit();
		let limit = input.TryNumber<uint>( "limit" ).value_or( 0 );
		Limit = limit && limit<readLimit ? limit : readLimit;
	}
	Args::Args( const QL::Input& input, SL sl )ε:
		ReadArgs{ input, sl }{
		if( Target(input)!=ETarget::Opc )
			throw TargetRefused( "history", sl );
		Opc = *input.FindPtr<jstring>( "opc" );
		eachNode( input, "history", sl, [this]( const jvalue& j ){ Nodes.push_back( node(j) ); } );
		THROW_IFSL( Nodes.empty(), "history names no nodes." );
		if( let c = input.FindPtr<jstring>("continuation"); c && c->size() ){
			Continuation = Decode( *c, Crc(), Nodes.size(), sl );
			if( Mode!=EMode::Raw && Continuation->next()>=Count() )
				throw invalid( "The continuation isn't one of the gateway's.", sl );
		}
	}
	GroupArgs::GroupArgs( const QL::Input& input, SL sl )ε:
		ReadArgs{ input, sl }{
		if( Target(input)!=ETarget::Group )
			throw TargetRefused( "history", sl );
		Group = groupId( input, "history", sl );
		eachNode( input, "history", sl, [&]( const jvalue& j ){
			auto node = ParseHistNode( j, sl );
			if( std::ranges::find(Nodes, node)!=Nodes.end() )
				throw Exception{ sl, ExceptionArgs{EHttpStatus::BadRequest}, "history names '{}' twice.", node.ToString() };
			Nodes.push_back( move(node) );
		} );
		THROW_IFSL( Nodes.empty(), "history names no nodes." );
		if( let c = input.FindPtr<jstring>("continuation"); c && c->size() ){
			try{
				Continuation = Str::Decode64( *c, true, sl );
			}
			catch( const Exception& ){
				throw invalid( "The continuation isn't one of the gateway's.", sl );
			}
		}
	}
	α Args::Crc()Ι->uint32_t{
		string bytes{ (char)Mode };
		bytes += Opc;
		for( let& node : Nodes ){
			bytes += '\0';
			bytes += node.ToString();
		}
		let ticks = [&bytes]( uint64_t t ){
			for( uint i=0; i<8; ++i )
				bytes += (char)( t>>(i*8) );
		};
		for( let& t : {Start, End} ){
			bytes += t ? '\1' : '\0';
			ticks( (uint64_t)t.value_or(0) );
		}
		bytes += Modified ? '\1' : '\0';
		bytes += Bounds ? '\1' : '\0';
		for( let t : Times )
			ticks( (uint64_t)t );
		ticks( std::bit_cast<uint64_t>(Interval) );
		bytes += Aggregate;
		return IO::Crc::Calc32c( bytes );
	}
	//An interval Hist::ToDuration refuses, 9e12 ms or more, is one over the whole range, as 0 is.
	Ω intervals( const ReadArgs& a )ι->Hist::Intervals{ return Hist::Intervals{ a.Range(), Hist::ToDuration(a.Interval).value_or(Duration::zero()) }; }
	α ReadArgs::Width()Ι->uint64_t{ return intervals( *this ).Width; }
	α ReadArgs::Uneven()Ι->bool{ return intervals( *this ).Uneven(); }
	α ReadArgs::Count()Ι->uint64_t{
		if( Mode==EMode::AtTime )
			return Times.size();
		return Mode==EMode::Processed && Start && End ? intervals( *this ).Count : 0;
	}
}
	α HistQL::FindEdit( sv command )ι->optional<EEdit>{
		for( uint8 i=0; i<EditCommands.size(); ++i ){
			if( EditCommands[i]==command )
				return (EEdit)i;
		}
		return nullopt;
	}
	α HistQL::PerformType( EEdit edit )ι->UA_PerformUpdateType{
		return edit==EEdit::Insert ? UA_PERFORMUPDATETYPE_INSERT : edit==EEdit::Replace ? UA_PERFORMUPDATETYPE_REPLACE : UA_PERFORMUPDATETYPE_UPDATE;
	}
	α HistQL::TargetRefused( EEdit edit, SL sl )ι->Exception{
		return TargetRefused( EditCommands[(uint8)edit], sl );
	}
	α HistQL::ToValue( const EditValue& e, sv node, const UA_DataType* type, sv command, SL sl )ε->Value{
		let typed = [&]()ε->Value{
			if( e.Data.is_null() )
				return Value{ e.Status.value_or(UA_STATUSCODE_GOOD) };
			try{
				return Value{ e.Data, type, sl };
			}
			catch( const std::exception& x ){
				if( type )
					throw Exception{ sl, {}, "{}:  {}'s value {} isn't a {}:  {}", command, node, serialize(e.Data), type->typeName, x.what() };
				throw Exception{ sl, {}, "{}:  {}'s value {} implies no type:  {}", command, node, serialize(e.Data), x.what() };
			}
		};
		Value y = typed();
		y.hasStatus = e.Status.has_value();
		y.status = e.Status.value_or( UA_STATUSCODE_GOOD );
		if( e.Source ){
			y.sourceTimestamp = *e.Source;
			y.hasSourceTimestamp = true;
		}
		if( e.Server ){
			y.serverTimestamp = *e.Server;
			y.hasServerTimestamp = true;
		}
		return y;
	}
namespace HistQL{
	EditArgs::EditArgs( EEdit edit, const QL::Input& input, SL sl )ε:
		Edit{ edit },
		Target{ HistQL::Target(input) }{
		if( Target==ETarget::Neither )
			throw TargetRefused( edit, sl );
		if( Target==ETarget::Opc )
			Opc = *input.FindPtr<jstring>( "opc" );
		else
			Group = groupId( input, Command(), sl );
		let parse = [this, sl]( const jvalue& j ){ return Target==ETarget::Opc ? ExNodeId{ node(j) } : ParseHistNode( j, sl ); };
		if( edit<EEdit::Purge ){
			let values = input.FindPtr<jvalue>( "values" );
			THROW_IFSL( !values || !values->is_array() || values->get_array().empty(), "{} names no values.", Command() );
			for( let& j : values->get_array() ){
				let& o = Json::AsObject( j, sl );
				let n = o.if_contains( "node" );
				THROW_IFSL( !n || n->is_null(), "{}:  a value names no node.", Command() );
				let data = o.if_contains( "value" );
				Values.push_back( {Slot(parse(*n)), data ? *data : jvalue{}, time(o.if_contains("source"), sl), time(o.if_contains("server"), sl), status(o.if_contains("status"), Command(), sl)} );
			}
			return;
		}
		eachNode( input, Command(), sl, [&]( const jvalue& j ){ Slot( parse(j) ); } );
		THROW_IFSL( Nodes.empty(), "{} names no nodes.", Command() );
		let start = time( input, "start", sl ), end = time( input, "end", sl );
		if( let times = input.FindPtr<jvalue>("times"); times && !times->is_null() ){//DeleteAtTime, else DeleteRawModified.
			THROW_IFSL( start || end, "{} takes a start and an end, or times, not both.", Command() );
			THROW_IFSL( !times->is_array() || times->get_array().empty(), "{} names no times.", Command() );
			for( let& j : times->get_array() )
				Times.push_back( UADateTime{j, sl}.UA() );
		}
		else{
			THROW_IFSL( !start || !end, "{} needs a start and an end, or times.", Command() );
			Start = *start; End = *end;
		}
	}
	α EditArgs::Slot( ExNodeId&& node )ι->uint{
		let p = std::ranges::find( Nodes, node );
		if( p!=Nodes.end() )
			return p-Nodes.begin();
		Nodes.push_back( move(node) );
		return Nodes.size()-1;
	}
	α EditArgs::NodeIds()Ι->vector<NodeId>{
		vector<NodeId> y; y.reserve( Nodes.size() );
		for( let& node : Nodes )
			y.emplace_back( node.nodeId );
		return y;
	}
}
	α HistQL::ReadLimit()ι->uint{
		let limit = Settings::FindNumber<uint>( "/gateway/hist/readLimit" ).value_or( Hist::Settings::DefaultReadLimit );
		return limit ? limit : std::numeric_limits<uint>::max();
	}
	α HistQL::Encode( const Hist::Proto::Continuation& c )ι->string{
		return Str::Encode64( c.SerializeAsString(), true );
	}
	α HistQL::Decode( sv text, uint32_t crc, uint nodes, SL sl )ε->Hist::Proto::Continuation{
		string bytes;
		try{
			bytes = Str::Decode64( text, true, sl );
		}
		catch( const Exception& ){
			throw invalid( "The continuation isn't one of the gateway's.", sl );
		}
		Hist::Proto::Continuation y;
		if( !y.ParseFromString(bytes) || y.counts_size()!=(int)nodes )
			throw invalid( "The continuation isn't one of the gateway's.", sl );
		if( y.crc()!=crc )
			throw invalid( "The continuation is for a read with other arguments.", sl );
		return y;
	}

	Ω timeJson( bool has, UA_DateTime t )ι->jvalue{ return has ? jvalue{ UADateTime{t}.ToJson() } : jvalue{}; }
	//nodes{ node status }, each node's status as the server answered it, Good where none is given.
	Ω nodesJson( const QL::TableQL& ql, const vector<jvalue>& nodes, const vector<StatusCode>& statuses )ι->jarray{
		let wantNode = ql.FindColumn("node") || ql.FindTable("node");
		let wantStatus = ql.FindColumn( "status" );
		jarray rows; rows.reserve( nodes.size() );
		for( uint i=0; i<nodes.size(); ++i ){
			jobject row;
			if( wantNode )
				row["node"] = nodes[i];
			if( wantStatus )
				row["status"] = i<statuses.size() ? statuses[i] : UA_STATUSCODE_GOOD;
			rows.push_back( move(row) );
		}
		return rows;
	}
	Ω updateType( UA_HistoryUpdateType t )ι->sv{
		switch( t ){
		case UA_HISTORYUPDATETYPE_INSERT: return "Insert";
		case UA_HISTORYUPDATETYPE_REPLACE: return "Replace";
		case UA_HISTORYUPDATETYPE_UPDATE: return "Update";
		case UA_HISTORYUPDATETYPE_DELETE: return "Delete";
		default: return "Unknown";
		}
	}
	α HistQL::ToJson( const QL::TableQL& ql, const vector<jvalue>& nodes, vector<ReadValue>&& values, sv continuation, const vector<StatusCode>& statuses )ι->jvalue{
		jobject y;
		if( ql.FindColumn("continuation") )
			y["continuation"] = continuation.size() ? jvalue{ continuation } : jvalue{};
		if( let valuesQL = ql.FindTable("values"); valuesQL ){
			let wantNode = valuesQL->FindColumn("node") || valuesQL->FindTable("node");
			let wantSource = valuesQL->FindColumn( "source" ), wantServer = valuesQL->FindColumn( "server" ), wantStatus = valuesQL->FindColumn( "status" );
			let wantValue = valuesQL->FindColumn( "value" ), wantBound = valuesQL->FindColumn( "bound" ), wantHeartbeat = valuesQL->FindColumn( "heartbeat" );
			let modificationQL = valuesQL->FindTable( "modification" );
			jarray rows; rows.reserve( values.size() );
			for( auto& v : values ){
				jobject row;
				if( wantNode )
					row["node"] = nodes[v.Slot];
				if( wantSource )
					row["source"] = timeJson( v.Data.hasSourceTimestamp, v.Data.sourceTimestamp );
				if( wantServer )
					row["server"] = timeJson( v.Data.hasServerTimestamp, v.Data.serverTimestamp );
				if( wantStatus )
					row["status"] = v.Data.hasStatus ? v.Data.status : UA_STATUSCODE_GOOD;
				if( wantValue )
					row["value"] = v.Data.ToJson();
				if( wantBound )
					row["bound"] = v.Bound;
				if( wantHeartbeat )
					row["heartbeat"] = v.Heartbeat;
				if( modificationQL && v.Modified ){
					jobject m;
					if( modificationQL->FindColumn("time") )
						m["time"] = UADateTime{ v.Modified->Time }.ToJson();
					if( modificationQL->FindColumn("type") )
						m["type"] = updateType( v.Modified->Type );
					if( modificationQL->FindColumn("user") )
						m["user"] = v.Modified->User;
					row["modification"] = move( m );
				}
				rows.push_back( move(row) );
			}
			y["values"] = move( rows );
		}
		if( let nodesQL = ql.FindTable("nodes"); nodesQL )
			y["nodes"] = nodesJson( *nodesQL, nodes, statuses );
		return y;
	}
	α HistQL::ToJson( const QL::TableQL& ql, const vector<NodeId>& nodes, vector<ReadValue>&& values, sv continuation, const vector<StatusCode>& statuses )ι->jvalue{
		return ToJson( ql, NodesJson(nodes), move(values), continuation, statuses );
	}
	α HistQL::ToJson( const QL::TableQL& ql, const vector<jvalue>& nodes, const vector<EditResult>& values, const vector<StatusCode>& statuses )ι->jvalue{
		jobject y;
		if( let valuesQL = ql.FindTable("values"); valuesQL ){
			let wantNode = valuesQL->FindColumn("node") || valuesQL->FindTable("node");
			let wantSource = valuesQL->FindColumn( "source" ), wantStatus = valuesQL->FindColumn( "status" );
			jarray rows; rows.reserve( values.size() );
			for( let& v : values ){
				jobject row;
				if( wantNode )
					row["node"] = nodes[v.Slot];
				if( wantSource )
					row["source"] = timeJson( v.Time.has_value(), v.Time.value_or(0) );
				if( wantStatus )
					row["status"] = v.Status;
				rows.push_back( move(row) );
			}
			y["values"] = move( rows );
		}
		if( let nodesQL = ql.FindTable("nodes"); nodesQL )
			y["nodes"] = nodesJson( *nodesQL, nodes, statuses );
		return y;
	}
}