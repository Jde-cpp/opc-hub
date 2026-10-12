#pragma once
//The fixture HistCollectorTests was built on (#224), shared with the subscription suite, HistSubscribeTests (#225).
#include <thread>
#include <jde/fwk/utils/Stopwatch.h>
#include <jde/ql/LocalQL.h>
#include <jde/access/Authorize.h>
#include <jde/opc/uatypes/DateTime.h>
#include "GatewayClientSocket.h"
#include "helpers.h"
#include "../../src/GatewayAppClient.h"
#include "../../src/hist/HistHost.h"
#include "../../src/ql/GatewayQL.h"
#include "../../src/UAClient.h"
#include "../../../OpcServer/src/globals.h"
#include "../../../OpcServer/src/UAServer.h"
#ifndef let
#define let const auto
#endif

namespace Jde::Opc::Gateway::Tests{
	using namespace std::chrono;
	namespace{
		constexpr sv PumpsUri{ "urn:jde:pumps" };
		//pumps.NodeSet2.xml:  RpmManual stores a change of 10 rpm, ABSOLUTE_VALUE, and Status1, with no HA Configuration, every
		//change.  Rpm2 is historized on the server and in no group here.
		constexpr UA_UInt32 RpmManual{ 6054 }, Status1{ 6011 }, Rpm2{ 6022 };
		struct Row final{ jvalue Node; optional<TimePoint> Source; StatusCode Status; jvalue Value; bool Bound; bool Heartbeat; };
		struct Page final{ vector<Row> Values; string Continuation; vector<std::pair<jvalue,StatusCode>> Statuses; };
		//A read's arguments, as HistTests' Request:  raw, or with Times at times, or with Interval and Aggregate aggregated.
		struct Request final{ optional<TimePoint> Start; optional<TimePoint> End; uint Limit{}; bool Bounds{}; bool Modified{}; vector<TimePoint> Times; optional<double> Interval; string Aggregate; };
		struct Edited final{ vector<StatusCode> Values; vector<StatusCode> Statuses; };
	}

	//A gateway group over the embedded OpcServer's historizing pump nodes, as HistCollectorTests and HistSubscribeTests drive it:
	//made through the group mutations, fed by the server's writes, read through the socket's QL, edited under an enforced grant.
	struct HistGroupFixture : ::testing::Test{
	protected:
		Ω SetUpTestCase()ε->void{
			if( !SelectServerCnnctn(OpcServerSlug) )
				CreateServerCnnctn();
			_connection = SelectServerCnnctn( OpcServerSlug )->Id;
			if( let ua = Server::FindUAServer(); ua ){
				size_t ns{};
				UAε( UA_Server_getNamespaceByName(ua->Ptr(), UA_STRING((char*)PumpsUri.data()), &ns) );
				_ns = (NsIndex)ns;
			}
		}
		α SetUp()->void override{
			if( !Server::FindUAServer() )
				GTEST_SKIP() << "HistGroupFixture need the embedded OpcServer.";
			if( !HistHost::Instance().Enabled() )
				GTEST_SKIP() << "The gateway keeps no history:  /gateway/hist.";
		}
		Ω User()ι->UserPK{ return AppClient()->UserPK(); }
		Ω Mutate( sv ql, jobject vars={} )ε->jvalue{ return BlockTAwait<jvalue>( QL::QLAwait<jvalue>{string{ql}, move(vars), User(), QLPtr()} ); }
		Ω Id( const jvalue& v )ε->uint32{ return Json::AsNumber<uint32>( Json::AsObject(v), "id" ); }
		//A node as the group's rows spell it, by URI, and as the server's clients name it, by index.
		Ω Ex( UA_UInt32 i )ι->jvalue{ return jobject{ {"nsu", PumpsUri}, {"i", i} }; }
		Ω Node( UA_UInt32 i )ι->NodeId{ return NodeId{ _ns, i }; }
		Ω Ms( TimePoint t )ι->TimePoint{ return floor<milliseconds>( t ); }//what a UA DateTime holds exactly.
		//Where a test's writes start:  just ahead, once each item's first value is in, so a first value the server stamped as it
		//sampled it, for a node whose value came with no source time, falls before the range the test compares.
		Ω Since()ι->TimePoint{ return Ms( Clock::now() )+10ms; }
		//An ad hoc group, its nodes added one by one.
		Ω CreateGroup( sv slug )ε->uint32{
			return Id( Mutate("createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection ){ id }", {{"name", Ƒ("Collector {}", slug)}, {"slug", slug}, {"connection", _connection}}) );
		}
		Ω AddNode( uint32 group, UA_UInt32 node, jobject thresholds={} )ε->uint32{ return AddNode( group, Ex(node), move(thresholds) ); }
		Ω AddNode( uint32 group, const jvalue& node, jobject thresholds={} )ε->uint32{
			string ql{ "createHistGroupNode( histGroupId:$group, node:$node" };
			jobject vars{ {"group", group}, {"node", serialize(node)} };
			for( let& [key,value] : thresholds ){
				ql += Ƒ( ", {}:${}", key, key );
				vars[key] = value;
			}
			return Id( Mutate(ql+" ){ id }", move(vars)) );
		}
		//The group goes, and its items with it, a second on (DeleteMonitoring):  waited for, so the next test counts none of them.
		Ω Purge( uint32 group, uint itemsBefore )ι->void{
			try{
				Mutate( "purgeHistGroup( id:$id )", {{"id", group}} );
				WaitItems( itemsBefore );
			}
			catch( const std::exception& e ){
				WARNT( ELogTags::Test, "purgeHistGroup({}):  {}", group, e.what() );
			}
		}
		Ω Items()ι->uint{ auto c = Collector(); return c && c->TryMonitoredNodes() ? c->TryMonitoredNodes()->Count() : 0; }
		Ω WaitItems( uint count )ε->void{
			Stopwatch sw;
			while( Items()!=count )
				sw.CheckTimeout( 15s, 10ms );
		}
		Ω Host( uint32 group )ε->sp<HistGroupHost>{
			auto host = HistHost::Instance().Find( group );
			THROW_IF( !host, "Group {} isn't live.", group );
			return host;
		}
		Ω Collector()ι->sp<UAClient>{ return UAClient::Find( OpcServerSlug, HistHost::Instance().Collector(OpcServerSlug) ); }
		Ω WaitSubscribed( const sp<HistGroupHost>& host, uint count )ε->void{
			Stopwatch sw;
			while( host->Subscribed()!=count )
				sw.CheckTimeout( 15s, 10ms );
		}
		//Until the server has described a member's node (HistGroupHost::Describe), the group takes no band for it.
		Ω WaitDescribed( const sp<HistGroupHost>& host, uint32 index )ε->HistDescription{
			Stopwatch sw;
			for( ;; ){
				if( auto d = host->Description(index); d && d->Enumeration && d->Ranged )
					return *d;
				sw.CheckTimeout( 15s, 10ms );
			}
		}
		//The group's records of a node from `from` on, read through the library, buffer and files both.
		Ω Stored( const sp<HistGroupHost>& host, uint32 index, TimePoint from )ε->vector<Opc::Hist::ReadValue>{
			return host->Group()->Read( {.Nodes={index}, .Start=UADateTime{from}.UA(), .Limit=10'000} ).Values;
		}
		Ω WaitStored( const sp<HistGroupHost>& host, uint32 index, TimePoint from, uint count )ε->vector<Opc::Hist::ReadValue>{
			let deadline = steady_clock::now()+15s;
			for( ;; ){
				auto stored = Stored( host, index, from );
				if( stored.size()>=count )
					return stored;
				if( steady_clock::now()>deadline ){
					string values;
					for( let& v : stored )
						values += Ƒ( " {}@{}", v.Value.has_value() ? serialize(Hist::ToUA(v.Value).ToJson()) : "none", v.Value.source_ts() );
					THROW( "Member {} stored {} of {} values from {}:{}", index, stored.size(), count, ToIsoString(from), values );
				}
				std::this_thread::sleep_for( 10ms );
			}
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
		Ω WriteRpm( double value, TimePoint source )ε->void{ Write( RpmManual, &value, UA_TYPES[UA_TYPES_DOUBLE], source ); }
		Ω WriteStatus( bool value, TimePoint source )ε->void{ UA_Boolean b = value; Write( Status1, &b, UA_TYPES[UA_TYPES_BOOLEAN], source ); }
		Ω Current( UA_UInt32 id )ι->Value{
			UA_ReadValueId read; UA_ReadValueId_init( &read );
			read.nodeId = Node( id );
			read.attributeId = UA_ATTRIBUTEID_VALUE;
			return Value{ UA_Server_read(Server::GetUAServer().Ptr(), &read, UA_TIMESTAMPSTORETURN_NEITHER) };
		}

		//One page through the gateway's socket, as the web sends it:  `target` is {group} or {opc}.
		Ω Read( jobject target, jarray nodes, const Request& request, sv continuation={} )ε->Page{
			let time = []( optional<TimePoint> t ){ return t ? jvalue{ UADateTime{*t}.ToJson() } : jvalue{}; };
			jvalue times;
			if( request.Times.size() ){
				jarray j;
				for( let t : request.Times )
					j.push_back( UADateTime{t}.ToJson() );
				times = move( j );
			}
			jobject vars{ {"nodes", move(nodes)}, {"start", time(request.Start)}, {"end", time(request.End)}, {"limit", request.Limit}, {"bounds", request.Bounds}, {"modified", request.Modified},
				{"times", move(times)}, {"interval", request.Interval ? jvalue{*request.Interval} : jvalue{}}, {"aggregate", request.Aggregate.size() ? jvalue{request.Aggregate} : jvalue{}},
				{"continuation", continuation.size() ? jvalue{continuation} : jvalue{}} };
			string args;
			for( auto&& [key,value] : target ){
				args += Ƒ( "{}: ${}, ", string{key}, string{key} );
				vars[key] = value;
			}
			let q = Ƒ( "history( {}nodes: $nodes, start: $start, end: $end, limit: $limit, returnBounds: $bounds, modified: $modified, times: $times, interval: $interval, aggregate: $aggregate, continuation: $continuation ){{ continuation values{{ node source server status value bound heartbeat }} nodes{{ node status }} }}", args );
			let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query(string{q}, vars, true) );
			TRACET( ELogTags::Test, "history: {}", serialize(value) );
			let& o = value.as_object();
			Page y;
			if( let c = o.if_contains("continuation"); c && c->is_string() )
				y.Continuation = c->get_string();
			for( let& j : Json::AsArray(o, "values") ){
				let& row = j.as_object();
				let at = [&]( sv name ){ let p = row.if_contains( name ); return p && !p->is_null() ? optional<TimePoint>{ UADateTime{*p}.Time() } : nullopt; };
				y.Values.push_back( {row.at("node"), at("source"), Json::AsNumber<StatusCode>(row, "status"), row.at("value"), Json::AsBool(row, "bound"), Json::AsBool(row, "heartbeat")} );
			}
			for( let& j : Json::AsArray(o, "nodes") )
				y.Statuses.emplace_back( j.as_object().at("node"), Json::AsNumber<StatusCode>(j.as_object(), "status") );
			return y;
		}
		Ω ReadAll( jobject target, jarray nodes, const Request& request )ε->vector<Row>{
			vector<Row> y;
			string continuation;
			for( uint i=0; i<100; ++i ){
				auto page = Read( target, nodes, request, continuation );
				for( auto& v : page.Values )
					y.push_back( move(v) );
				continuation = page.Continuation;
				if( continuation.empty() )
					return y;
			}
			THROW( "100 pages and still a continuation." );
		}
		Ω Group( uint32 group )ι->jobject{ return {{"group", group}}; }
		Ω Opc()ι->jobject{ return {{"opc", OpcServerSlug}}; }
		//What the two hosts' reads must agree on:  each value's source time, status and value, in order.
		Ω Agree( const vector<Row>& group, const vector<Row>& server )ι->::testing::AssertionResult{
			if( group.size()!=server.size() )
				return ::testing::AssertionFailure() << "the group read " << group.size() << " values, the server " << server.size();
			for( uint i=0; i<group.size(); ++i ){
				let& g = group[i]; let& s = server[i];
				if( g.Source!=s.Source || g.Status!=s.Status || g.Value!=s.Value )
					return ::testing::AssertionFailure() << "value " << i << ":  the group's " << (g.Source ? ToIsoString(*g.Source) : "no source") << " " << serialize(g.Value) << " status " << g.Status
						<< ", the server's " << (s.Source ? ToIsoString(*s.Source) : "no source") << " " << serialize(s.Value) << " status " << s.Status;
			}
			return ::testing::AssertionSuccess();
		}

		//An edit through the socket, `group` set.
		Ω Edit( string q, jobject vars )ε->Edited{
			let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query(move(q), move(vars), true) );
			TRACET( ELogTags::Test, "edit: {}", serialize(value) );
			let& o = value.as_object();
			Edited y;
			for( let& j : Json::FindDefaultArray(o, "values") )
				y.Values.push_back( Json::AsNumber<StatusCode>(j.as_object(), "status") );
			for( let& j : Json::AsArray(o, "nodes") )
				y.Statuses.push_back( Json::AsNumber<StatusCode>(j.as_object(), "status") );
			return y;
		}
		struct Sample final{ UA_UInt32 Node; TimePoint Source; jvalue Value; };
		Ω Update( sv command, uint32 group, const vector<Sample>& samples )ε->Edited{
			jarray values;
			for( let& sample : samples )
				values.push_back( jobject{ {"node", Ex(sample.Node)}, {"source", UADateTime{sample.Source}.ToJson()}, {"value", sample.Value} } );
			return Edit( Ƒ("{}( group: $group, values: $values ){{ values{{ node source status }} nodes{{ node status }} }}", command), {{"group", group}, {"values", move(values)}} );
		}
		Ω PurgeRange( uint32 group, UA_UInt32 node, TimePoint start, TimePoint end )ε->Edited{
			return Edit( "purgeHistory( group: $group, nodes: $nodes, start: $start, end: $end ){ nodes{ node status } }", {{"group", group}, {"nodes", Ex(node)}, {"start", UADateTime{start}.ToJson()}, {"end", UADateTime{end}.ToJson()}} );
		}
		Ω PurgeAt( uint32 group, UA_UInt32 node, const vector<TimePoint>& times )ε->Edited{
			jarray j;
			for( let t : times )
				j.push_back( UADateTime{t}.ToJson() );
			return Edit( "purgeHistory( group: $group, nodes: $nodes, times: $times ){ values{ node source status } nodes{ node status } }", {{"group", group}, {"nodes", Ex(node)}, {"times", move(j)}} );
		}
		//The hist_groups resource enforced, with the gateway's user granted everything on it through a role of its own, or
		//soft-deleted again, as it ships:  a group's history edits need a grant on an enforced resource (spec *Authorization*).
		Ω Enforce( bool on )ε->void{
			auto app = AppClient();
			let slug = jobject{ {"slug", "histGroups"}, {"schema", "gateway"} };
			let acl = app->Acl();
			let reached = [&]( bool active, sv change ){
				for( uint i=0; acl->FindActiveResourcePK("gateway", "histGroups", "").has_value()!=active; ++i ){
					THROW_IF( i==200, "The histGroups resource's {} didn't reach the gateway's authorizer.", change );
					std::this_thread::sleep_for( 50ms );
				}
			};
			app->QuerySync<jvalue>( "deleteResource( schemaName:$schema, slug:$slug, criteria:null )", slug );
			reached( false, "delete" );
			if( !on )
				return;
			constexpr sv roleSlug{ "GatewayHistGroupEditor" };
			if( app->QuerySync("role(slug:$slug, deleted:null){id}", {{"slug", roleSlug}}).empty() ){
				let role = app->QuerySync<jobject>( "createRole( slug:$slug, name:$name ){id}", {{"slug", roleSlug}, {"name", "Gateway group history editor"}} );
				let roleId = Json::AsNumber<Access::RolePK::Type>( role.at("id") );
				app->QuerySync<jvalue>( "addRole( id:$roleId, permissionRight:{allowed:$allowed, denied:0, resource:{schemaName:\"gateway\", slug:\"histGroups\"}} )", {{"roleId", roleId}, {"allowed", underlying(Access::ERights::All)}} );
				app->QuerySync<jvalue>( "createAcl( identity:{ id:$userId }, role:{id:$roleId} )", {{"userId", User().Value}, {"roleId", roleId}} );
			}
			app->QuerySync<jvalue>( "restoreResource( schemaName:$schema, slug:$slug, criteria:null )", slug );
			reached( true, "restore" );
		}
		inline static ServerCnnctnPK _connection{};
		inline static NsIndex _ns{};
	};
}