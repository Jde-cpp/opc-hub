//Historian 5c (#224):  a gateway group collecting, read and edited (spec *Collection*, *Reads*, *Edits*) - the collector's items
//feeding the library, history with `group` in its three modes, and the edits with `group` under a grant on an enforced
//resource.  And the differential test the plan sets:  a group over the embedded OpcServer's historizing pump nodes, with the
//thresholds their HA Configurations give, read beside a pass-through read of the server's own history of the same range,
//through a connection drop that the group marks as a gap and the server, which never lost a value, doesn't.
#include <thread>
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/log/MemoryLog.h>
#include <jde/fwk/utils/Stopwatch.h>
#include <jde/ql/LocalQL.h>
#include <jde/access/Authorize.h>
#include <jde/opc/uatypes/DateTime.h>
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "../src/GatewayAppClient.h"
#include "../src/hist/HistHost.h"
#include "../src/ql/GatewayQL.h"
#include "../src/UAClient.h"
#include "../../OpcServer/src/globals.h"
#include "../../OpcServer/src/UAServer.h"
#define let const auto

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

	struct HistCollectorTests : ::testing::Test{
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
				GTEST_SKIP() << "HistCollectorTests need the embedded OpcServer.";
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
		static ServerCnnctnPK _connection;
		static NsIndex _ns;
	};
	ServerCnnctnPK HistCollectorTests::_connection{};
	NsIndex HistCollectorTests::_ns{};

	//The differential test (plan *Differential test*):  a group over RpmManual and Status1 with the thresholds their HA
	//Configurations give stores what OpcServer stores itself, so a group read and a pass-through read of the same range
	//agree:  raw, at times and aggregated.  Then the collector's connection drops while a value is written:  the group marks
	//the gap with Bad_DataLost before the value that ends it, the server holds the value alone, and Status1, which didn't
	//change, comes back with nothing new.
	TEST_F( HistCollectorTests, AgreesWithThePassThrough ){
		let itemsBefore = Items();
		let group = CreateGroup( "differential" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let rpm = AddNode( group, RpmManual, {{"exceptionDeviation", 10}, {"exceptionDeviationFormat", 0}} );
		let status = AddNode( group, Status1 );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		ASSERT_NO_THROW( WaitDescribed(host, rpm) );//its band
		//Each item's first value, the node's value now, is stored as the first after the join.
		let joined = Clock::now()-24h*365;
		ASSERT_NO_THROW( WaitStored(host, rpm, joined, 1) );
		ASSERT_NO_THROW( WaitStored(host, status, joined, 1) );

		//A jump off the value the node holds now, so both hosts' bands start from the same stored value, then changes inside
		//the 10 rpm band and past it.  Status1 flips from its value now at every write:  an item reports a change of value or
		//status, so a write that repeats the value reaches OpcServer's history and not the group's, which is sampling.
		let base = Current( RpmManual ).AsNumber<double>()+1000;
		const vector<double> rpms{ base, base+4, base+11, base+15, base+30, base+35, base-100 };
		let on = !*(const UA_Boolean*)Current( Status1 ).value.data;
		let t0 = Since();
		for( uint i=0; i<rpms.size(); ++i ){
			WriteRpm( rpms[i], t0+milliseconds{10*i} );
			WriteStatus( i%2==0 ? on : !on, t0+milliseconds{10*i+5} );
		}
		let rpmStored = WaitStored( host, rpm, t0, 4 );
		let statusStored = WaitStored( host, status, t0, rpms.size() );
		const Request range{ .Start=t0, .End=t0+1s, .Limit=100 };
		let groupRpm = ReadAll( Group(group), {Ex(RpmManual)}, range );
		let serverRpm = ReadAll( Opc(), {Node(RpmManual).ToJson()}, range );
		ASSERT_EQ( groupRpm.size(), 4u ) << "the band dropped the three inside it";
		EXPECT_TRUE( Agree(groupRpm, serverRpm) );
		EXPECT_EQ( groupRpm[1].Value, jvalue{base+11} );
		let groupStatus = ReadAll( Group(group), {Ex(Status1)}, range );
		EXPECT_EQ( groupStatus.size(), rpms.size() ) << "a Boolean stores every change";
		EXPECT_TRUE( Agree(groupStatus, ReadAll(Opc(), {Node(Status1).ToJson()}, range)) );
		for( let& row : groupRpm ){
			EXPECT_EQ( row.Node, Ex(RpmManual) ) << "a node comes back as the read named it";
			EXPECT_FALSE( row.Heartbeat );
		}
		//A node named by the server's index reads the same, spelled the same way back.
		let byIndex = ReadAll( Group(group), {Node(RpmManual).ToJson()}, range );
		EXPECT_TRUE( Agree(byIndex, groupRpm) );
		ASSERT_FALSE( byIndex.empty() );
		EXPECT_EQ( byIndex[0].Node, jvalue{Node(RpmManual).ToJson()} );

		//At times, stepped from the records either side, and aggregated over 20 ms intervals.
		const Request atTimes{ .Limit=100, .Times={t0+15ms, t0+25ms, t0+41ms, t0+60ms, t0+500ms} };
		let groupAt = ReadAll( Group(group), {Ex(RpmManual)}, atTimes );
		EXPECT_EQ( groupAt.size(), atTimes.Times.size() );
		EXPECT_TRUE( Agree(groupAt, ReadAll(Opc(), {Node(RpmManual).ToJson()}, atTimes)) );
		for( let aggregate : {"Count", "Maximum", "Average"} ){
			SCOPED_TRACE( aggregate );
			const Request processed{ .Start=t0, .End=t0+80ms, .Limit=100, .Interval=20, .Aggregate=aggregate };
			let groupProcessed = ReadAll( Group(group), {Ex(RpmManual)}, processed );
			EXPECT_EQ( groupProcessed.size(), 4u );
			EXPECT_TRUE( Agree(groupProcessed, ReadAll(Opc(), {Node(RpmManual).ToJson()}, processed)) );
		}

		//The collector's connection drops, and a value is written while it is down:  the rebuild's first value is later than
		//the last the group had, so the group stores Bad_DataLost at the break, then the value.  Status1 comes back unchanged.
		auto collector = Collector();
		ASSERT_TRUE( collector );
		let broke = Clock::now();
		UAClient::ConnectionLost( sp<UAClient>{collector} );
		let missed = Ms( Clock::now() );
		WriteRpm( base+500, missed );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 6) ) << "no gap marker and value after the reconnect";
		const Request after{ .Start=t0, .End=missed+1s, .Limit=100 };
		let groupAfter = ReadAll( Group(group), {Ex(RpmManual)}, after );
		let serverAfter = ReadAll( Opc(), {Node(RpmManual).ToJson()}, after );
		ASSERT_EQ( groupAfter.size(), 6u );
		let& marker = groupAfter[4];
		EXPECT_EQ( marker.Status, UA_STATUSCODE_BADDATALOST );
		EXPECT_FALSE( marker.Value.is_number() ) << "a marker holds no value:  " << serialize( marker.Value );
		ASSERT_TRUE( marker.Source );
		EXPECT_GE( *marker.Source, Ms(broke) );
		EXPECT_LE( *marker.Source, missed );
		EXPECT_EQ( groupAfter[5].Value, jvalue{base+500} );
		EXPECT_EQ( groupAfter[5].Source, missed );
		vector<Row> unmarked;
		for( let& row : groupAfter ){
			if( row.Status!=UA_STATUSCODE_BADDATALOST )
				unmarked.push_back( row );
		}
		EXPECT_TRUE( Agree(unmarked, serverAfter) ) << "apart from the gap marker";
		std::this_thread::sleep_for( 1s );//a publishing interval or two for anything else the rebuild delivers.
		EXPECT_EQ( Stored(host, status, t0).size(), statusStored.size() ) << "Status1's value after the break repeats the last it delivered:  nothing was lost";
		EXPECT_EQ( rpmStored.size(), 4u );
	}

	//history with `group`:  pages by the library's continuation either way, answers Bad_NodeIdUnknown for a node that isn't a
	//member, and refuses a member named twice, a group the gateway doesn't host, an aggregate it doesn't serve, and a call that
	//names both a group and a server, or neither.
	TEST_F( HistCollectorTests, ReadsAGroupsHistory ){
		let itemsBefore = Items();
		let group = CreateGroup( "reads" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let rpm = AddNode( group, RpmManual );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		ASSERT_NO_THROW( WaitStored(host, rpm, Clock::now()-24h*365, 1) );
		let t0 = Since();
		let base = Current( RpmManual ).AsNumber<double>()+1000;
		for( uint i=0; i<5; ++i )
			WriteRpm( base+i*100, t0+milliseconds{10*i} );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 5) );

		const Request forward{ .Start=t0, .End=t0+1s, .Limit=2 };
		let first = Read( Group(group), {Ex(RpmManual)}, forward );
		ASSERT_EQ( first.Values.size(), 2u );
		ASSERT_FALSE( first.Continuation.empty() );
		EXPECT_EQ( first.Values[0].Source, t0 );
		let all = ReadAll( Group(group), {Ex(RpmManual)}, forward );
		ASSERT_EQ( all.size(), 5u );
		let reverse = ReadAll( Group(group), {Ex(RpmManual)}, {.Start=t0+1s, .End=t0, .Limit=2} );
		ASSERT_EQ( reverse.size(), 5u );
		EXPECT_EQ( reverse.front().Value, all.back().Value );
		let bounded = ReadAll( Group(group), {Ex(RpmManual)}, {.Start=t0+5ms, .End=t0+25ms, .Limit=100, .Bounds=true} );
		ASSERT_EQ( bounded.size(), 4u );
		EXPECT_TRUE( bounded.front().Bound && bounded.back().Bound );
		EXPECT_EQ( bounded.front().Source, t0 );
		EXPECT_EQ( bounded.back().Source, t0+30ms );
		EXPECT_THROW( Read(Group(group), {Ex(RpmManual)}, {.Start=t0, .End=t0+2s, .Limit=2}, first.Continuation), GatewayErrorResponse ) << "another read's continuation";

		let mixed = Read( Group(group), {Ex(Rpm2), Ex(RpmManual)}, {.Start=t0, .End=t0+1s, .Limit=100} );
		EXPECT_EQ( mixed.Values.size(), 5u );
		ASSERT_EQ( mixed.Statuses.size(), 2u );
		EXPECT_EQ( mixed.Statuses[0].second, UA_STATUSCODE_BADNODEIDUNKNOWN );
		EXPECT_EQ( mixed.Statuses[1].second, UA_STATUSCODE_GOOD );
		let empty = Read( Group(group), {Ex(RpmManual)}, {.Start=t0-24h*400, .End=t0-24h*399, .Limit=100} );//before the group was made.
		EXPECT_TRUE( empty.Values.empty() );
		ASSERT_EQ( empty.Statuses.size(), 1u );
		EXPECT_EQ( empty.Statuses[0].second, UA_STATUSCODE_GOODNODATA );

		EXPECT_THROW( Read(Group(group), {Ex(RpmManual), Node(RpmManual).ToJson()}, forward), GatewayErrorResponse ) << "one member by two spellings";
		EXPECT_THROW( Read(Group(group), {Ex(RpmManual), Ex(RpmManual)}, forward), GatewayErrorResponse ) << "one node twice";
		EXPECT_THROW( Read(Group(999'999), {Ex(RpmManual)}, forward), GatewayErrorResponse ) << "no such group";
		EXPECT_THROW( Read(Group(group), {Ex(RpmManual)}, {.Start=t0, .End=t0+1s, .Limit=100, .Interval=10, .Aggregate="Range"}), GatewayErrorResponse ) << "an aggregate the library doesn't serve";
		auto both = Group( group );
		both["opc"] = OpcServerSlug;
		EXPECT_THROW( Read(both, {Ex(RpmManual)}, forward), GatewayErrorResponse );
		EXPECT_THROW( Read({}, {Ex(RpmManual)}, forward), GatewayErrorResponse );
	}

	//The edits with `group`:  refused while nothing enforces hist_groups, whatever the caller may do elsewhere, and with the
	//resource enforced and granted, each answers Part 11's results, a value typed as the group stored its node, and a node
	//that isn't a member Bad_NodeIdUnknown.  The modified read returns what each edit replaced, by the caller.
	TEST_F( HistCollectorTests, EditsNeedAnEnforcedGrant ){
		let itemsBefore = Items();
		let group = CreateGroup( "edits" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let rpm = AddNode( group, RpmManual );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		ASSERT_NO_THROW( WaitStored(host, rpm, Clock::now()-24h*365, 1) );
		let t0 = Since();
		let base = Current( RpmManual ).AsNumber<double>()+1000;
		WriteRpm( base, t0 );
		WriteRpm( base+100, t0+20ms );
		ASSERT_NO_THROW( WaitStored(host, rpm, t0, 2) );

		Enforce( false );
		EXPECT_THROW( Update("createHistory", group, {{RpmManual, t0+10ms, 1}}), GatewayErrorResponse ) << "an edit nothing enforces";
		absl::Cleanup open = []{ try{ Enforce(false); }catch( const std::exception& e ){ ADD_FAILURE() << "histGroups wasn't opened again:  " << e.what(); } };//before:  an Enforce that throws can leave it restored.
		Enforce( true );

		let inserted = Update( "createHistory", group, {{RpmManual, t0+10ms, 1}, {RpmManual, t0, 2}, {Rpm2, t0+10ms, 3}} );
		EXPECT_EQ( inserted.Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED, UA_STATUSCODE_BADENTRYEXISTS, UA_STATUSCODE_BADNODEIDUNKNOWN}) );
		EXPECT_EQ( inserted.Statuses, (vector<StatusCode>{UA_STATUSCODE_GOOD, UA_STATUSCODE_BADNODEIDUNKNOWN}) );
		EXPECT_EQ( Update("updateHistory", group, {{RpmManual, t0+20ms, 250}, {RpmManual, t0+30ms, 9}}).Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED, UA_STATUSCODE_BADNOENTRYEXISTS}) );
		EXPECT_EQ( Update("upsertHistory", group, {{RpmManual, t0, 7}, {RpmManual, t0+40ms, 8}}).Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED, UA_STATUSCODE_GOODENTRYINSERTED}) );
		const Request range{ .Start=t0, .End=t0+50ms, .Limit=100 };
		vector<jvalue> values;
		for( let& row : ReadAll(Group(group), {Ex(RpmManual)}, range) )
			values.push_back( row.Value );
		EXPECT_EQ( values, (vector<jvalue>{7.0, 1.0, 250.0, 8.0}) ) << "a json integer takes the Double the group stored";

		EXPECT_EQ( PurgeAt(group, RpmManual, {t0+10ms, t0+11ms}).Values, (vector<StatusCode>{UA_STATUSCODE_GOOD, UA_STATUSCODE_BADNOENTRYEXISTS}) );
		EXPECT_EQ( PurgeRange(group, RpmManual, t0+20ms, t0+40ms).Statuses, (vector<StatusCode>{UA_STATUSCODE_GOOD}) ) << "a group's range holds both ends";
		EXPECT_EQ( PurgeRange(group, RpmManual, t0+20ms, t0+40ms).Statuses, (vector<StatusCode>{UA_STATUSCODE_BADNODATA}) );
		values.clear();
		for( let& row : ReadAll(Group(group), {Ex(RpmManual)}, range) )
			values.push_back( row.Value );
		EXPECT_EQ( values, (vector<jvalue>{7.0}) );

		let modified = ReadAll( Group(group), {Ex(RpmManual)}, {.Start=t0, .End=t0+50ms, .Limit=100, .Modified=true} );
		EXPECT_EQ( modified.size(), 7u ) << "an insert, a replace, an upsert's two, and three deletes";
		try{//a value that can't take the node's type refuses the call, naming the node and the value.
			Update( "createHistory", group, {{RpmManual, t0+45ms, "not a number"}} );
			ADD_FAILURE() << "a value that can't be a Double was taken";
		}
		catch( const GatewayErrorResponse& e ){
			EXPECT_NE( string{e.what()}.find("\"not a number\" isn't a Double"), string::npos ) << e.what();
		}
	}
	//A node that leaves and joins again within the second its item outlives it (DeleteMonitoring) joins that item, which sends
	//no initial value:  the collector reads the node's value instead, and the new member's history starts with it, once,
	//after the Bad_DataLost a rejoin's break writes (spec *Gaps*).
	TEST_F( HistCollectorTests, ARejoinReadsTheItemsValue ){
		let itemsBefore = Items();
		let group = CreateGroup( "rejoin" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let first = AddNode( group, RpmManual );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		let joined = Clock::now()-24h*365;
		ASSERT_NO_THROW( WaitStored(host, first, joined, 1) );
		Mutate( "purgeHistGroupNode( id:$id )", {{"id", first}} );
		let again = AddNode( group, RpmManual );
		ASSERT_NE( again, first ) << "a rejoin is a new row, and a new index";
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		let stored = WaitStored( host, again, joined, 2 );
		ASSERT_EQ( stored.size(), 2u );
		EXPECT_EQ( stored[0].Value.status(), UA_STATUSCODE_BADDATALOST );
		EXPECT_FALSE( stored[0].Value.has_value() );
		EXPECT_TRUE( stored[1].Value.has_value() );
		std::this_thread::sleep_for( 1s );//a publishing interval or two:  the item's next value is a change, not its value now again.
		EXPECT_EQ( Stored(host, again, joined).size(), 2u );
		EXPECT_EQ( Items(), itemsBefore+1 ) << "the one item, joined, not a second";
	}
	//A namespace the server registers at the end moves no member's index:  neither the pass that sees it, here a member's join,
	//nor a rebuild's check makes the group's items again (spec *Collector contract*).
	TEST_F( HistCollectorTests, AnAppendedNamespaceMovesNothing ){
		if( !Logging::FindLogger<Logging::MemoryLog>() )
			Logging::AddLogger( mu<Logging::MemoryLog>() );
		let itemsBefore = Items();
		let group = CreateGroup( "appended" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let status = AddNode( group, Status1 );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		let logged = [&]( sv text ){
			return Logging::Find( [&]( const Logging::Entry& e ){ let m = e.Message(); return m.contains( host->Guid() ) && m.contains( text ); } );
		};
		let passes = [&]( uint count ){//each create's INFO
			Stopwatch sw;
			while( logged("items created on").size()<count )
				sw.CheckTimeout( 15s, 10ms );
			return logged( "items created on" );
		};
		ASSERT_NO_THROW( passes(1) );
		auto ua = Server::FindUAServer();
		UA_Server_addNamespace( ua->Ptr(), "urn:jde:tests:appended:join" );
		AddNode( group, RpmManual );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		vector<Logging::Entry> created;
		ASSERT_NO_THROW( created = passes(2) );
		ASSERT_EQ( created.size(), 2u );
		EXPECT_TRUE( created[1].Message().contains("1 of 1 items") ) << created[1].Message();

		//The rebuild's values wait for the check, so once the value written while down is stored, the check has decided.
		UA_Server_addNamespace( ua->Ptr(), "urn:jde:tests:appended:rebuild" );
		auto collector = Collector();
		ASSERT_TRUE( collector );
		let on = !*(const UA_Boolean*)Current( Status1 ).value.data;
		UAClient::ConnectionLost( sp<UAClient>{collector} );
		let missed = Ms( Clock::now() );
		WriteStatus( on, missed );
		ASSERT_NO_THROW( WaitStored(host, status, missed, 1) );
		EXPECT_TRUE( logged("namespaces moved").empty() );
		EXPECT_EQ( logged("items created on").size(), 2u ) << "the rebuild's items kept";
	}
	//Each member's DataType, read once its item is made, is the type the group's edits take, and goes into its join's NodeAdded
	//while no flush has taken that (spec *Edits*).
	TEST_F( HistCollectorTests, MembersTakeTheirDataType ){
		let itemsBefore = Items();
		let group = CreateGroup( "types" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let rpm = AddNode( group, RpmManual );
		let status = AddNode( group, Status1 );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		let declared = [&]( uint32 index )->const UA_DataType*{
			for( let& r : host->Group()->Buffer() ){
				if( let added = std::get_if<Opc::Hist::NodeAdded>(&r); added && added->Index==index )
					return added->DataType;
			}
			return nullptr;
		};
		Stopwatch sw;
		while( !declared(rpm) || !declared(status) )
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		EXPECT_EQ( declared(rpm), &UA_TYPES[UA_TYPES_DOUBLE] );
		EXPECT_EQ( declared(status), &UA_TYPES[UA_TYPES_BOOLEAN] );
		EXPECT_EQ( host->Group()->DataType(status), &UA_TYPES[UA_TYPES_BOOLEAN] );
	}
	//An abstract DataType of namespace 0, as Number, declares none:  the group's edits take the type of the values it stored,
	//which a Number's may change (spec *Record format*).
	TEST_F( HistCollectorTests, AnAbstractDataTypeDeclaresNone ){
		constexpr UA_UInt32 Count{ 9026 };//added here, holding an Int32.
		auto attributes = UA_VariableAttributes_default;
		UA_Int32 zero{};
		UA_Variant_setScalar( &attributes.value, &zero, &UA_TYPES[UA_TYPES_INT32] );
		attributes.dataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_NUMBER );
		attributes.accessLevel = UA_ACCESSLEVELMASK_READ | UA_ACCESSLEVELMASK_WRITE;
		ASSERT_EQ( UA_Server_addVariableNode(Server::GetUAServer().Ptr(), Node(Count), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
			UA_QUALIFIEDNAME(_ns, (char*)"Count"), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr), UA_STATUSCODE_GOOD );
		absl::Cleanup remove = [=]{ UA_Server_deleteNode( Server::GetUAServer().Ptr(), Node(Count), true ); };
		let itemsBefore = Items();
		let group = CreateGroup( "abstract" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let count = AddNode( group, Count );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		ASSERT_NO_THROW( WaitDescribed(host, count) );
		let joined = Clock::now()-24h*365;
		ASSERT_NO_THROW( WaitStored(host, count, joined, 1) );
		let buffer = host->Group()->Buffer();
		let join = std::ranges::find_if( buffer, [=]( let& r ){ let added = std::get_if<Opc::Hist::NodeAdded>( &r ); return added && added->Index==count; } );
		ASSERT_NE( join, buffer.end() );
		let declared = std::get<Opc::Hist::NodeAdded>( *join ).DataType;
		EXPECT_EQ( declared, nullptr ) << declared->typeName;
		EXPECT_EQ( host->Group()->DataType(count), &UA_TYPES[UA_TYPES_INT32] ) << "the type of the value it stored";
		const UA_Double half{ 1.5 };
		Write( Count, &half, UA_TYPES[UA_TYPES_DOUBLE], Since() );
		ASSERT_NO_THROW( WaitStored(host, count, joined, 2) );
		EXPECT_EQ( host->Group()->DataType(count), &UA_TYPES[UA_TYPES_DOUBLE] );
	}
	//A percent-of-range band takes the InstrumentRange or EURange the server gives the node, read once its item is made, and
	//a format edit takes the other;  a node without the range its format needs stores every change (spec *Compression*).
	TEST_F( HistCollectorTests, RangesFromTheServer ){
		constexpr UA_UInt32 Pump3Rpm{ 6032 }, Pump4Rpm{ 6042 };//pump3's InstrumentRange is 0-1500 and its EURange 0-3000;  pump4 has the EURange alone.
		let itemsBefore = Items();
		let group = CreateGroup( "ranges" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let pump3 = AddNode( group, Pump3Rpm, {{"exceptionDeviation", 1}, {"exceptionDeviationFormat", 2}} );
		let pump4 = AddNode( group, Pump4Rpm, {{"exceptionDeviation", 1}, {"exceptionDeviationFormat", 2}} );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		let thresholds = [&]( uint32 index ){ return host->Group()->FindThresholds( index ).value(); };
		Stopwatch sw;
		while( !thresholds(pump3).Range )
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		EXPECT_EQ( thresholds(pump3).ExceptionDeviation, 1 );
		EXPECT_EQ( thresholds(pump3).Range->High, 1500 ) << "its InstrumentRange";
		EXPECT_FALSE( thresholds(pump4).ExceptionDeviation ) << "no InstrumentRange:  every change is stored";
		Mutate( "updateHistGroupNode( id:$id, exceptionDeviationFormat:3 )", {{"id", pump3}} );
		EXPECT_EQ( thresholds(pump3).Range->High, 3000 ) << "its EURange";
		Mutate( "updateHistGroupNode( id:$id, exceptionDeviationFormat:3 )", {{"id", pump4}} );
		EXPECT_EQ( thresholds(pump4).ExceptionDeviation, 1 );
		ASSERT_TRUE( thresholds(pump4).Range );
		EXPECT_EQ( thresholds(pump4).Range->High, 3000 );
		EXPECT_FALSE( host->FindThresholds(pump4)->Range ) << "the rows', which keep no range";
	}
	//A description Describe lost, rather than was refused, the watch asks for again (Redescribe):  here a percent-of-range band
	//waits while the server refuses the translate of a node's two range properties, and takes the InstrumentRange once asked again.
	TEST_F( HistCollectorTests, ALostDescriptionIsAskedAgain ){
		constexpr UA_UInt32 Pump3Rpm{ 6032 };//its InstrumentRange is 0-1500.
		auto& config = *UA_Server_getConfig( Server::GetUAServer().Ptr() );
		let limit = config.maxNodesPerTranslateBrowsePathsToNodeIds;
		config.maxNodesPerTranslateBrowsePathsToNodeIds = 1;
		absl::Cleanup restore = [&]{ config.maxNodesPerTranslateBrowsePathsToNodeIds = limit; };
		let itemsBefore = Items();
		let group = CreateGroup( "redescribe" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		let pump3 = AddNode( group, Pump3Rpm, {{"exceptionDeviation", 1}, {"exceptionDeviationFormat", 2}} );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		Stopwatch sw;
		optional<HistDescription> described;
		while( !(described = host->Description(pump3)) || !described->Enumeration )//its DataType, read in the same Describe.
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		EXPECT_FALSE( described->Ranged ) << "the translate was refused";
		let thresholds = [&]{ return host->Group()->FindThresholds( pump3 ).value(); };
		EXPECT_FALSE( thresholds().ExceptionDeviation ) << "its band waits";
		config.maxNodesPerTranslateBrowsePathsToNodeIds = limit;
		host->Redescribe();
		ASSERT_NO_THROW( WaitDescribed(host, pump3) );
		EXPECT_EQ( thresholds().ExceptionDeviation, 1 );
		ASSERT_TRUE( thresholds().Range );
		EXPECT_EQ( thresholds().Range->High, 1500 ) << "its InstrumentRange";
	}
	//An enumeration's values arrive as Int32, which the library can't tell from a quantity, so its node takes no band
	//(spec *Compression*):  ServerStatus.State, of namespace 0's ServerState, a variable of Enumeration itself, which
	//open62541's kind for it says is one as well, and the IA examples' DeviceHealth, whose DeviceHealthEnumeration only the
	//server's definition of it says is one.
	TEST_F( HistCollectorTests, EnumerationsTakeNoBand ){
		constexpr UA_UInt32 Mode{ 9029 };//added here:  of Enumeration itself, i=29, holding an Int32.
		auto attributes = UA_VariableAttributes_default;
		UA_Int32 one{ 1 };
		UA_Variant_setScalar( &attributes.value, &one, &UA_TYPES[UA_TYPES_INT32] );
		attributes.dataType = UA_NODEID_NUMERIC( 0, UA_NS0ID_ENUMERATION );
		attributes.accessLevel = UA_ACCESSLEVELMASK_READ;
		ASSERT_EQ( UA_Server_addVariableNode(Server::GetUAServer().Ptr(), Node(Mode), UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER), UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES),
			UA_QUALIFIEDNAME(_ns, (char*)"Mode"), UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE), attributes, nullptr, nullptr), UA_STATUSCODE_GOOD );
		absl::Cleanup remove = [=]{ UA_Server_deleteNode( Server::GetUAServer().Ptr(), Node(Mode), true ); };
		let itemsBefore = Items();
		let group = CreateGroup( "enumerations" );
		absl::Cleanup purge = [=]{ Purge( group, itemsBefore ); };
		const jobject band{ {"exceptionDeviation", 1}, {"exceptionDeviationFormat", 0} };
		let state = AddNode( group, jobject{{"nsu", "http://opcfoundation.org/UA/"}, {"i", UA_NS0ID_SERVER_SERVERSTATUS_STATE}}, band );
		let health = AddNode( group, jobject{{"nsu", "http://opcfoundation.org/UA/IA/Examples/"}, {"i", 6002}}, band );
		let mode = AddNode( group, Mode, band );
		let rpm = AddNode( group, RpmManual, band );
		auto host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 4) );
		let thresholds = [&]( uint32 index ){ return host->Group()->FindThresholds( index ).value(); };
		for( let [index, enumeration] : {std::pair{state, true}, std::pair{health, true}, std::pair{mode, true}, std::pair{rpm, false}} ){
			HistDescription described;
			ASSERT_NO_THROW( described = WaitDescribed(host, index) );
			EXPECT_EQ( described.Enumeration, enumeration ) << "member " << index;
			EXPECT_EQ( thresholds(index).ExceptionDeviation.has_value(), !enumeration ) << "member " << index;
			EXPECT_EQ( host->FindThresholds(index)->ExceptionDeviation, 1 ) << "the row keeps it";
		}
	}
}