#pragma once
//History read and edited through the gateway's socket, over the embedded OpcServer's pump nodes:  the half HistTests'
//pass-through suites and the group suites, through HistGroupFixture, share.
#include <jde/opc/uatypes/DateTime.h>
#include "GatewayClientSocket.h"
#include "helpers.h"
#include "Pumps.h"
#ifndef let
#define let const auto
#endif

namespace Jde::Opc::Gateway::Tests{
	namespace{
		struct Modification final{ TimePoint Time; string Type; string User; };
		struct Row final{ jvalue Node; optional<TimePoint> Source; optional<TimePoint> Server; StatusCode Status; jvalue Value; bool Bound; bool Heartbeat; optional<Modification> Modified; };
		//Each node's status as the answer's `nodes` lists them, the node spelled as the call spelled it.
		using NodeStatuses = vector<std::pair<jvalue,StatusCode>>;
		struct Page final{ vector<Row> Values; string Continuation; NodeStatuses Statuses; };
		//A read's arguments:  raw, or with Times at times, or with Interval, in milliseconds, and Aggregate aggregated.  Nodes,
		//by index, for a suite that spells them no other way.
		struct Request final{ vector<UA_UInt32> Nodes; optional<TimePoint> Start; optional<TimePoint> End; uint Limit{}; bool Bounds{}; bool Modified{}; vector<TimePoint> Times; optional<double> Interval; string Aggregate; };
		struct Edited final{ vector<StatusCode> Values; NodeStatuses Statuses; };
		struct Sample final{ UA_UInt32 Node; TimePoint Source; jvalue Value; };
	}

	struct HistReadFixture : ::testing::Test{
	protected:
		Ω SetUpTestSuite()ε->void{
			GetConnection( OpcServerSlug );
			_ns = PumpsNs();
		}
		//The tests write to the server itself, so an external one (/testing/embeddedOpcServer false) can't serve them.
		α SetUp()->void override{
			if( !Server::FindUAServer() )
				GTEST_SKIP() << "The history suites need the embedded OpcServer.";
		}
		//A node as the group's rows spell it, by URI, and as the server's clients name it, by index, as a NodeId or json.
		Ω Ex( UA_UInt32 i )ι->jvalue{ return jobject{ {"nsu", PumpsUri}, {"i", i} }; }
		Ω Node( UA_UInt32 i )ι->NodeId{ return NodeId{ _ns, i }; }
		Ω ByIndex( UA_UInt32 i )ι->jvalue{ return Node( i ).ToJson(); }
		Ω ByIndex( const vector<UA_UInt32>& ids )ι->jarray{
			jarray y;
			for( let id : ids )
				y.push_back( ByIndex(id) );
			return y;
		}
		Ω Group( uint32 group )ι->jobject{ return {{"group", group}}; }
		Ω Opc()ι->jobject{ return {{"opc", OpcServerSlug}}; }
		//A node as an edit with `target` names it:  a group's member by URI, a server's node by index.
		Ω Spell( const jobject& target, UA_UInt32 i )ι->jvalue{ return target.contains( "group" ) ? Ex( i ) : ByIndex( i ); }
		Ω Status( const NodeStatuses& statuses, const jvalue& node )ε->StatusCode{
			let p = std::ranges::find( statuses, node, &NodeStatuses::value_type::first );
			THROW_IF( p==statuses.end(), "No status for {}.", serialize(node) );
			return p->second;
		}
		Ω Codes( const NodeStatuses& statuses )ι->vector<StatusCode>{
			vector<StatusCode> y;
			for( let& s : statuses )
				y.push_back( s.second );
			return y;
		}
		//A write as a client's reaches the server, with the time the source gave it.
		Ω Write( UA_UInt32 id, const void* value, const UA_DataType& type, TimePoint source )ε->void{
			UA_WriteValue write; UA_WriteValue_init( &write );
			write.nodeId = Node( id );
			write.attributeId = UA_ATTRIBUTEID_VALUE;
			UA_Variant_setScalar( &write.value.value, (void*)value, &type );
			write.value.hasValue = true;
			write.value.sourceTimestamp = UADateTime{ source }.UA();
			write.value.hasSourceTimestamp = true;
			UAε( UA_Server_write(Server::GetUAServer().Ptr(), &write) );
		}

		//One page through the gateway's socket, the arguments as variables, as the web sends them:  `target` is {group} or {opc}.
		Ω Read( jobject target, jarray nodes, const Request& request, sv continuation={} )ε->Page{
			let time = []( optional<TimePoint> t ){ return t ? jvalue{ UADateTime{*t}.ToJson() } : jvalue{}; };
			jvalue times;//null, as the web sends an argument its mode doesn't take.
			if( request.Times.size() ){
				jarray j;
				for( let t : request.Times )
					j.push_back( UADateTime{t}.ToJson() );
				times = move( j );
			}
			jobject vars{ {"nodes", move(nodes)}, {"start", time(request.Start)}, {"end", time(request.End)}, {"limit", request.Limit}, {"bounds", request.Bounds}, {"modified", request.Modified},
				{"times", move(times)}, {"interval", request.Interval ? jvalue{*request.Interval} : jvalue{}}, {"aggregate", request.Aggregate.size() ? jvalue{request.Aggregate} : jvalue{}},
				{"continuation", continuation.size() ? jvalue{continuation} : jvalue{}} };
			let q = Ƒ( "history( {}nodes: $nodes, start: $start, end: $end, limit: $limit, returnBounds: $bounds, modified: $modified, times: $times, interval: $interval, aggregate: $aggregate, continuation: $continuation ){{ continuation values{{ node source server status value bound heartbeat modification{{ time type user }} }} nodes{{ node status }} }}", Target(move(target), vars) );
			let value = Socket().QuerySync( string{q}, vars );
			TRACET( ELogTags::Test, "history: {}", serialize(value) );
			let& o = value.as_object();
			Page y;
			if( let c = o.if_contains("continuation"); c && c->is_string() )
				y.Continuation = c->get_string();
			for( let& j : Json::AsArray(o, "values") ){
				let& row = j.as_object();
				let at = [&]( sv name ){ let p = row.if_contains( name ); return p && !p->is_null() ? optional<TimePoint>{ UADateTime{*p}.Time() } : nullopt; };
				optional<Modification> modified;
				if( let m = row.if_contains("modification"); m && m->is_object() )
					modified = Modification{ UADateTime{m->as_object().at("time")}.Time(), Json::AsString(m->as_object(), "type"), Json::AsString(m->as_object(), "user") };
				y.Values.push_back( {row.at("node"), at("source"), at("server"), Json::AsNumber<StatusCode>(row, "status"), row.at("value"), Json::AsBool(row, "bound"), Json::AsBool(row, "heartbeat"), move(modified)} );
			}
			y.Statuses = nodeStatuses( o );
			return y;
		}
		//Every page, following the continuation to the last.  pages takes each page's size, and between runs between pages.
		Ω ReadAll( jobject target, jarray nodes, const Request& request, vector<uint>* pages=nullptr, function<void()> between={} )ε->vector<Row>{
			vector<Row> y;
			string continuation;
			for( uint i=0; i<100; ++i ){
				auto page = Read( target, nodes, request, continuation );
				if( pages )
					pages->push_back( page.Values.size() );
				for( auto& v : page.Values )
					y.push_back( move(v) );
				continuation = page.Continuation;
				if( continuation.empty() )
					return y;
				if( between )
					between();
			}
			THROW( "100 pages and still a continuation." );
		}

		//An edit through the socket, its target already in `q` and `vars`.
		Ω Edit( string q, jobject vars )ε->Edited{
			let value = Socket().QuerySync( move(q), move(vars) );
			TRACET( ELogTags::Test, "edit: {}", serialize(value) );
			let& o = value.as_object();
			Edited y;
			for( let& j : Json::FindDefaultArray(o, "values") )
				y.Values.push_back( Json::AsNumber<StatusCode>(j.as_object(), "status") );
			y.Statuses = nodeStatuses( o );
			return y;
		}
		//createHistory, updateHistory or upsertHistory.
		Ω Update( sv command, jobject target, const vector<Sample>& samples )ε->Edited{
			jarray values;
			for( let& sample : samples )
				values.push_back( jobject{ {"node", Spell(target, sample.Node)}, {"source", UADateTime{sample.Source}.ToJson()}, {"value", sample.Value} } );
			jobject vars{ {"values", move(values)} };
			let args = Target( move(target), vars );
			return Edit( Ƒ("{}( {}values: $values ){{ values{{ node source status }} nodes{{ node status }} }}", command, args), move(vars) );
		}
		Ω Update( sv command, uint32 group, const vector<Sample>& samples )ε->Edited{ return Update( command, Group(group), samples ); }
		Ω PurgeRange( jobject target, UA_UInt32 node, TimePoint start, TimePoint end )ε->Edited{
			jobject vars{ {"nodes", Spell(target, node)}, {"start", UADateTime{start}.ToJson()}, {"end", UADateTime{end}.ToJson()} };
			let args = Target( move(target), vars );
			return Edit( Ƒ("purgeHistory( {}nodes: $nodes, start: $start, end: $end ){{ nodes{{ node status }} }}", args), move(vars) );
		}
		Ω PurgeRange( uint32 group, UA_UInt32 node, TimePoint start, TimePoint end )ε->Edited{ return PurgeRange( Group(group), node, start, end ); }
		Ω PurgeAt( jobject target, UA_UInt32 node, const vector<TimePoint>& times )ε->Edited{
			jarray j;
			for( let t : times )
				j.push_back( UADateTime{t}.ToJson() );
			jobject vars{ {"nodes", Spell(target, node)}, {"times", move(j)} };
			let args = Target( move(target), vars );
			return Edit( Ƒ("purgeHistory( {}nodes: $nodes, times: $times ){{ values{{ node source status }} nodes{{ node status }} }}", args), move(vars) );
		}
		Ω PurgeAt( uint32 group, UA_UInt32 node, const vector<TimePoint>& times )ε->Edited{ return PurgeAt( Group(group), node, times ); }
		inline static NsIndex _ns{};
	private:
		//`target`'s arguments, `group: $group, ` or `opc: $opc, `, their values moved into `vars`.
		Ω Target( jobject target, jobject& vars )ι->string{
			string args;
			for( auto&& [key,value] : target ){
				args += Ƒ( "{}: ${}, ", string{key}, string{key} );
				vars[key] = move( value );
			}
			return args;
		}
		Ω nodeStatuses( const jobject& o )ε->NodeStatuses{
			NodeStatuses y;
			for( let& j : Json::AsArray(o, "nodes") )
				y.emplace_back( j.as_object().at("node"), Json::AsNumber<StatusCode>(j.as_object(), "status") );
			return y;
		}
	};
}