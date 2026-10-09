//Historian 3A (#214):  history with `opc`, the gateway reading a server's own history for the caller (spec *Pass-through*)
//- here the embedded OpcServer's pump nodes, written on the server with source times on three past days, so a read
//pages across its day files, and read back through the gateway's QL by time, as the web will.  3B (#215):  the history
//edits with `opc`, each sent as a HistoryUpdate over the caller's session, and read back as modified values.
#include <thread>
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/settings.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/historian/Historian.h>
#include <jde/opc/uatypes/DateTime.h>
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "../src/GatewayAppClient.h"
#include "../src/UAClient.h"
#include "../../OpcServer/src/globals.h"
#include "../../OpcServer/src/UAServer.h"
#include "../../OpcServer/src/access/OpcAuthorize.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using namespace std::chrono;
	namespace{
		//pumps.NodeSet2.xml:  Rpm4 stores a change of 15 rpm, RpmManual one of 10, and Status2 isn't historized.
		constexpr UA_UInt32 Rpm4{ 6042 }, RpmManual{ 6054 }, Status2{ 6021 };
		struct Written final{ UA_UInt32 Node; TimePoint Time; double Value; };
		struct Row final{ NodeId Node; optional<TimePoint> Source; optional<TimePoint> Server; StatusCode Status; jvalue Value; bool Bound; };
		struct Page final{ vector<Row> Values; string Continuation; flat_map<NodeId,StatusCode> Statuses; };
		struct Request final{ vector<UA_UInt32> Nodes; optional<TimePoint> Start; optional<TimePoint> End; uint Limit{}; bool Bounds{}; };
	}

	struct HistTests : ::testing::Test{
	protected:
		Ω SetUpTestCase()ε->void{
			let ua = Server::FindUAServer();
			if( !ua )//SetUp skips each test.
				return;
			if( !SelectServerCnnctn(OpcServerSlug) )
				CreateServerCnnctn();
			THROW_IF( !ua->History().Enabled(), "The embedded OpcServer keeps no history:  /opcServer/hist." );
			size_t ns{};
			UAε( UA_Server_getNamespaceByName(ua->Ptr(), UA_STRING((char*)"urn:jde:pumps"), &ns) );
			_ns = (NsIndex)ns;
			//Three whole days before today:  before the value each node took at the start, which today's file holds, and
			//days no midnight moves while the tests run.
			_day = floor<days>( Clock::now() )-days{ 3 };
			//Rpm4 is dense, six a day, and RpmManual sparse, one on the first day and one on the last.
			for( uint day=0; day<3; ++day ){
				for( uint i=0; i<6; ++i )
					Write( Rpm4, 100+day*600+i*100, At(day, 10+i*10) );
			}
			Write( RpmManual, 50, At(0, 15) );
			Write( RpmManual, 70, At(2, 45) );
			THROW_IF( !BlockAny(ua->History().Group()->Flush()), "The flush didn't write all it took." );
		}
		//The tests write to the server itself, so an external one (/testing/embeddedOpcServer false) can't serve them.
		α SetUp()->void override{
			if( !Server::FindUAServer() )
				GTEST_SKIP() << "HistTests need the embedded OpcServer.";
		}
		Ω Node( UA_UInt32 id )ι->NodeId{ return NodeId{ _ns, id }; }
		Ω At( uint day, uint second )ι->TimePoint{ return _day+days{ day }+seconds{ second }; }
		//A write as a client's reaches the server, with the time the source gave it.
		Ω Write( UA_UInt32 id, double value, TimePoint source )ε->void{
			let node = Node( id );
			UA_WriteValue write; UA_WriteValue_init( &write );
			write.nodeId = node;
			write.attributeId = UA_ATTRIBUTEID_VALUE;
			UA_Variant_setScalar( &write.value.value, &value, &UA_TYPES[UA_TYPES_DOUBLE] );
			write.value.hasValue = true;
			write.value.sourceTimestamp = UADateTime{ source }.UA();
			write.value.hasSourceTimestamp = true;
			UAε( UA_Server_write(Server::GetUAServer().Ptr(), &write) );
			_written.push_back( {id, source, value} );
		}
		//One page through the gateway, the arguments as variables, as the web sends them.
		Ω Read( const Request& request, sv continuation={} )ε->Page{
			jarray nodes;
			for( let id : request.Nodes )
				nodes.push_back( Node(id).ToJson() );
			let time = []( optional<TimePoint> t ){ return t ? jvalue{ UADateTime{*t}.ToJson() } : jvalue{}; };
			jobject vars{ {"opc", OpcServerSlug}, {"nodes", move(nodes)}, {"start", time(request.Start)}, {"end", time(request.End)}, {"limit", request.Limit}, {"bounds", request.Bounds},
				{"continuation", continuation.size() ? jvalue{continuation} : jvalue{}} };
			let q = "history( opc: $opc, nodes: $nodes, start: $start, end: $end, limit: $limit, returnBounds: $bounds, continuation: $continuation ){ continuation values{ node source server status value bound } nodes{ node status } }";
			let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query(q, vars, true) );
			TRACET( ELogTags::Test, "history: {}", serialize(value) );
			let& o = value.as_object();
			Page y;
			if( let c = o.if_contains("continuation"); c && c->is_string() )
				y.Continuation = c->get_string();
			for( let& j : Json::AsArray(o, "values") ){
				let& row = j.as_object();
				let at = [&]( sv name ){ let p = row.if_contains( name ); return p && !p->is_null() ? optional<TimePoint>{ UADateTime{*p}.Time() } : nullopt; };
				y.Values.push_back( {NodeId{Json::AsObject(row, "node")}, at("source"), at("server"), Json::AsNumber<StatusCode>(row, "status"), row.at("value"), Json::AsBool(row, "bound")} );
			}
			for( let& j : Json::AsArray(o, "nodes") )
				y.Statuses.emplace( NodeId{Json::AsObject(j.as_object(), "node")}, Json::AsNumber<StatusCode>(j.as_object(), "status") );
			return y;
		}
		//Every page, following the continuation to the last.  pages takes each page's size, and between runs between pages.
		Ω ReadAll( const Request& request, vector<uint>* pages=nullptr, function<void()> between={} )ε->vector<Row>{
			vector<Row> y;
			string continuation;
			for( uint i=0; i<100; ++i ){
				auto page = Read( request, continuation );
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
		//What was written for the nodes inside the range, which holds its start and not its end whichever way it flows,
		//in the read's order:  by time, then the request's order of nodes.
		Ω Expected( const Request& request )ι->vector<Written>{
			vector<Written> y;
			let reverse = *request.Start>*request.End;
			let earlier = reverse ? *request.End : *request.Start, later = reverse ? *request.Start : *request.End;
			for( let id : request.Nodes ){
				for( let& w : _written ){
					if( w.Node==id && (reverse ? w.Time>earlier && w.Time<=later : w.Time>=earlier && w.Time<later) )
						y.push_back( w );
				}
			}
			std::ranges::stable_sort( y, [reverse]( let& a, let& b ){ return reverse ? a.Time>b.Time : a.Time<b.Time; } );
			return y;
		}
		Ω Same( const vector<Row>& rows, const vector<Written>& expected )ι->::testing::AssertionResult{
			if( rows.size()!=expected.size() )
				return ::testing::AssertionFailure() << rows.size() << " values, expected " << expected.size();
			for( uint i=0; i<rows.size(); ++i ){
				let& r = rows[i]; let& e = expected[i];
				if( !(r.Node==Node(e.Node)) || r.Source!=e.Time || !r.Value.is_double() || r.Value.as_double()!=e.Value || r.Status!=UA_STATUSCODE_GOOD || !r.Server )
					return ::testing::AssertionFailure() << "value " << i << ":  " << r.Node.ToString() << " " << (r.Source ? ToIsoString(*r.Source) : "no source") << " " << serialize(r.Value) << " status " << r.Status << ", expected " << Node(e.Node).ToString() << " " << ToIsoString(e.Time) << " " << e.Value;
			}
			return ::testing::AssertionSuccess();
		}
		static inline NsIndex _ns{};
		static inline TimePoint _day{};
		static inline vector<Written> _written;
	};

	//Rpm4's three days, six values each, seven a page:  a page runs over a day boundary within the call, following the
	//server's continuation point, and the next page resumes by time.
	TEST_F( HistTests, PagesAcrossDayFilesForward ){
		const Request request{ .Nodes={Rpm4}, .Start=At(0, 0), .End=At(3, 0), .Limit=7 };
		vector<uint> pages;
		let rows = ReadAll( request, &pages );
		EXPECT_EQ( pages, (vector<uint>{7, 7, 4}) );
		EXPECT_TRUE( Same(rows, Expected(request)) );
		let page = Read( request );
		ASSERT_EQ( page.Statuses.size(), 1u );
		EXPECT_EQ( page.Statuses.begin()->second, UA_STATUSCODE_GOOD );
	}

	TEST_F( HistTests, PagesAcrossDayFilesReverse ){
		const Request request{ .Nodes={Rpm4}, .Start=At(3, 0), .End=At(0, 0), .Limit=7 };
		vector<uint> pages;
		let rows = ReadAll( request, &pages );
		EXPECT_EQ( pages, (vector<uint>{7, 7, 4}) );
		EXPECT_TRUE( Same(rows, Expected(request)) );
		ASSERT_EQ( rows.size(), 18u );
		EXPECT_EQ( rows.front().Source, At(2, 60) );
	}

	//A dense and a sparse node merge by source time up to the horizon:  the sparse one's values land among the dense
	//one's, each once, and the pages stay full.
	TEST_F( HistTests, MergesADenseAndASparseNode ){
		for( let reverse : {false, true} ){
			SCOPED_TRACE( reverse ? "reverse" : "forward" );
			const Request request{ .Nodes={RpmManual, Rpm4}, .Start=At(reverse ? 3 : 0, 0), .End=At(reverse ? 0 : 3, 0), .Limit=6 };
			vector<uint> pages;
			let rows = ReadAll( request, &pages );
			EXPECT_EQ( pages, (vector<uint>{6, 6, 6, 2}) );
			EXPECT_TRUE( Same(rows, Expected(request)) );
		}
	}

	//The continuation is by time, so the session the first page used can go between pages:  the next page opens a new
	//one and resumes where the last left off.
	TEST_F( HistTests, ResumesAfterTheConnectionDrops ){
		const Request request{ .Nodes={Rpm4, RpmManual}, .Start=At(0, 0), .End=At(3, 0), .Limit=6 };
		uint dropped{};
		let rows = ReadAll( request, nullptr, [&]{
			for( auto& client : UAClient::LiveClients() ){
				if( client->Slug()==OpcServerSlug && UAClient::RemoveClient(move(client)) )
					++dropped;
			}
		} );
		EXPECT_GE( dropped, 1u );
		EXPECT_TRUE( Same(rows, Expected(request)) );
	}

	//With returnBounds, each node's value at or before the start and its first at or after the end come too, the other way
	//round in a reverse read, marked:  the opening one on the first page, the closing one after the last page's values.
	//A later page drops the bound the server gives for the resume time.
	TEST_F( HistTests, ReturnsBounds ){
		for( let reverse : {false, true} ){
			SCOPED_TRACE( reverse ? "reverse" : "forward" );
			let start = reverse ? At(2, 45) : At(0, 15), end = reverse ? At(0, 15) : At(2, 45);
			vector<uint> pages;
			let rows = ReadAll( {.Nodes={Rpm4}, .Start=start, .End=end, .Limit=6, .Bounds=true}, &pages );
			EXPECT_EQ( pages, (vector<uint>{6, 6, 5}) );
			ASSERT_EQ( rows.size(), 17u );
			EXPECT_TRUE( rows.front().Bound );
			EXPECT_EQ( rows.front().Source, reverse ? At(2, 50) : At(0, 10) );
			EXPECT_TRUE( rows.back().Bound );
			EXPECT_EQ( rows.back().Source, reverse ? At(0, 10) : At(2, 50) );
			for( uint i=1; i+1<rows.size(); ++i )
				EXPECT_FALSE( rows[i].Bound ) << i;
			const vector<Row> inside{ rows.begin()+1, rows.end()-1 };
			EXPECT_TRUE( Same(inside, Expected({.Nodes={Rpm4}, .Start=start, .End=end})) );
		}
	}

	//An end alone reads backward from it, the common "last N values", and a start alone forward from it;  either pages on,
	//the backward one bounded at 1601 from its second page.  With bounds, the end's bound comes first, and the bound the
	//server gives at 1601, an end the read doesn't have, is dropped.
	TEST_F( HistTests, ReadsFromOneEnd ){
		let last = Read( {.Nodes={Rpm4}, .End=At(3, 0), .Limit=4} );
		ASSERT_EQ( last.Values.size(), 4u );
		EXPECT_EQ( last.Values[0].Source, At(2, 60) );
		EXPECT_EQ( last.Values[3].Source, At(2, 30) );
		ASSERT_FALSE( last.Continuation.empty() );
		let before = Read( {.Nodes={Rpm4}, .End=At(3, 0), .Limit=4}, last.Continuation );
		ASSERT_EQ( before.Values.size(), 4u );
		EXPECT_EQ( before.Values[0].Source, At(2, 20) );
		EXPECT_EQ( before.Values[3].Source, At(1, 50) );

		let bounded = ReadAll( {.Nodes={Rpm4}, .End=At(3, 0), .Limit=4, .Bounds=true} );
		ASSERT_FALSE( bounded.empty() );
		EXPECT_TRUE( bounded.front().Bound );
		const vector<Row> inside{ bounded.begin()+1, bounded.end() };
		EXPECT_TRUE( Same(inside, Expected({.Nodes={Rpm4}, .Start=At(3, 0), .End=At(0, 0)})) );

		let from = Read( {.Nodes={Rpm4}, .Start=At(2, 30), .Limit=3} );
		ASSERT_EQ( from.Values.size(), 3u );
		EXPECT_EQ( from.Values[0].Source, At(2, 30) );
		EXPECT_EQ( from.Values[2].Source, At(2, 50) );
		ASSERT_FALSE( from.Continuation.empty() );
		let on = Read( {.Nodes={Rpm4}, .Start=At(2, 30), .Limit=3}, from.Continuation );//past the test's days:  the value the node took at the start follows 2:60.
		ASSERT_GE( on.Values.size(), 1u );
		EXPECT_EQ( on.Values[0].Source, At(2, 60) );
	}

	//readLimit caps a page when the read passes no limit, and 0 is no limit:  the whole range on one page.
	TEST_F( HistTests, ReadLimitZeroIsNone ){
		let saved = Settings::FindNumber<uint>( "/gateway/hist/readLimit" ).value_or( Hist::Settings::DefaultReadLimit );
		absl::Cleanup restore = [saved]{ try{ Settings::Set("/gateway/hist/readLimit", saved); }catch( const std::exception& ){} };//every later read takes it.
		const Request request{ .Nodes={Rpm4, RpmManual}, .Start=At(0, 0), .End=At(3, 0) };
		Settings::Set( "/gateway/hist/readLimit", 2 );
		let two = Read( request );
		EXPECT_EQ( two.Values.size(), 2u );
		EXPECT_FALSE( two.Continuation.empty() );
		Settings::Set( "/gateway/hist/readLimit", 0 );
		let all = Read( request );
		EXPECT_TRUE( all.Continuation.empty() );
		EXPECT_TRUE( Same(all.Values, Expected(request)) );
	}

	//A continuation is the read's own:  it pages at any limit, and is refused with other nodes or times, as is a read that
	//names a group beside the server.
	TEST_F( HistTests, RefusesAnotherReadsContinuation ){
		let first = Read( {.Nodes={Rpm4}, .Start=At(0, 0), .End=At(3, 0), .Limit=4} );
		ASSERT_FALSE( first.Continuation.empty() );
		let wider = Read( {.Nodes={Rpm4}, .Start=At(0, 0), .End=At(3, 0), .Limit=10}, first.Continuation );
		ASSERT_EQ( wider.Values.size(), 10u );
		EXPECT_EQ( wider.Values[0].Source, At(0, 50) );
		EXPECT_THROW( Read({.Nodes={Rpm4, RpmManual}, .Start=At(0, 0), .End=At(3, 0), .Limit=4}, first.Continuation), GatewayErrorResponse );
		EXPECT_THROW( Read({.Nodes={Rpm4}, .Start=At(0, 0), .End=At(2, 0), .Limit=4}, first.Continuation), GatewayErrorResponse );
		EXPECT_THROW( Read({.Nodes={Rpm4}, .Start=At(0, 0), .End=At(3, 0), .Limit=4}, "not-a-continuation"), GatewayErrorResponse );
		jobject vars{ {"opc", OpcServerSlug}, {"node", Node(Rpm4).ToJson()}, {"start", UADateTime{At(0, 0)}.ToJson()}, {"end", UADateTime{At(3, 0)}.ToJson()} };
		EXPECT_THROW( (BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("history( opc: $opc, group: 1, nodes: $node, start: $start, end: $end ){ values{ value } }", vars, true) )), GatewayErrorResponse );
	}

	//A node the server refuses answers with the server's status beside the others' values, and a range with nothing in
	//it with its Good_NoData:  the gateway checks nothing of its own.  A modified read passes through as one, which
	//OpcServer serves (#210):  nothing is edited here, so it holds no values, with the node's Good_NoData.
	TEST_F( HistTests, AnswersWithTheServersStatus ){
		let page = Read( {.Nodes={Status2, Rpm4}, .Start=At(0, 0), .End=At(1, 0), .Limit=100} );
		EXPECT_EQ( page.Values.size(), 6u );
		for( let& v : page.Values )
			EXPECT_EQ( v.Node, Node(Rpm4) );
		ASSERT_EQ( page.Statuses.size(), 2u );
		EXPECT_EQ( page.Statuses.at(Node(Status2)), UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );
		EXPECT_EQ( page.Statuses.at(Node(Rpm4)), UA_STATUSCODE_GOOD );

		let nothing = Read( {.Nodes={RpmManual}, .Start=At(1, 0), .End=At(2, 0), .Limit=100} );
		EXPECT_TRUE( nothing.Values.empty() );
		EXPECT_TRUE( nothing.Continuation.empty() );
		EXPECT_EQ( nothing.Statuses.at(Node(RpmManual)), UA_STATUSCODE_GOODNODATA );

		jobject vars{ {"opc", OpcServerSlug}, {"node", Node(Rpm4).ToJson()}, {"start", UADateTime{At(0, 0)}.ToJson()}, {"end", UADateTime{At(3, 0)}.ToJson()} };
		let modified = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("history( opc: $opc, nodes: $node, start: $start, end: $end, modified: true ){ values{ value } nodes{ node status } }", vars, true) );
		let& o = modified.as_object();
		EXPECT_TRUE( Json::AsArray(o, "values").empty() );
		let& nodes = Json::AsArray( o, "nodes" );
		ASSERT_EQ( nodes.size(), 1u );
		EXPECT_EQ( Json::AsNumber<StatusCode>(nodes[0].as_object(), "status"), UA_STATUSCODE_GOODNODATA );
	}

	namespace{
		//pumps.NodeSet2.xml:  Rpm3 stores a change of 15 rpm, with no time intervals, Status1 is a historized Boolean, and
		//Rpm2 a historized Double no read here uses.
		constexpr UA_UInt32 Rpm2{ 6022 }, Rpm3{ 6032 }, Status1{ 6011 }, Unknown{ 999'999 };
		struct Edited final{ vector<StatusCode> Values; flat_map<NodeId,StatusCode> Statuses; };
		struct Sample final{ UA_UInt32 Node; TimePoint Source; jvalue Value; };
		struct Modified final{ NodeId Node; TimePoint Source; jvalue Value; string Type; string User; TimePoint Time; };
	}
	//The edits on a day of their own, two before the reads', through the gateway's QL as the web will send them.  OpcServer
	//edits history only for a user granted Update, or Delete, on an enforced resource (spec *Authorization*), so the suite
	//enforces nodeIds with the gateway's user granted everything on it, and opens it again after.
	struct HistEditTests : HistTests{
	protected:
		Ω SetUpTestCase()ε->void{
			let ua = Server::FindUAServer();
			if( !ua )
				return;
			if( !SelectServerCnnctn(OpcServerSlug) )
				CreateServerCnnctn();
			THROW_IF( !ua->History().Enabled(), "The embedded OpcServer keeps no history:  /opcServer/hist." );
			size_t ns{};
			UAε( UA_Server_getNamespaceByName(ua->Ptr(), UA_STRING((char*)"urn:jde:pumps"), &ns) );
			_ns = (NsIndex)ns;
			_editDay = floor<days>( Clock::now() )-days{ 5 };
			Enforce( true );
		}
		Ω TearDownTestCase()ι->void{
			try{
				if( Server::FindUAServer() )
					Enforce( false );
			}
			catch( const std::exception& e ){
				ERRT( ELogTags::Test, "HistEditTests left nodeIds enforced:  {}", e.what() );
			}
		}
		//The root nodeIds resource, enforced with the gateway's user granted everything on it through a role of its own, or
		//soft-deleted again, as it ships.  The grant is made while the resource is unenforced, which is when the delegated
		//admin check passes, through OpcServer's own app client, as its HistoryTests make theirs.
		Ω Enforce( bool on )ε->void{
			auto app = Server::AppClient();
			let& schema = app->ResourceSchema;//"opc", or "opc.<resource>" with /opcServer/resource, as OpcServer's startup names it.
			let nodeSlug = jobject{ {"slug","nodeIds"}, {"schema", schema} };
			auto& authorizer = static_cast<Server::OpcAuthorize&>( *Server::GetSchema().Authorizer );
			let reached = [&]( bool active, sv change ){
				for( uint i=0; authorizer.FindActiveResourcePK(schema, "nodeIds", "").has_value()!=active; ++i ){
					THROW_IF( i==200, "The nodeIds resource's {} didn't reach OpcServer's authorizer.", change );
					std::this_thread::sleep_for( 50ms );
				}
			};
			app->QuerySync<jvalue>( "deleteResource( schemaName:$schema, slug:$slug, criteria:null )", nodeSlug );
			reached( false, "delete" );
			if( !on )
				return;
			constexpr sv roleSlug{ "GatewayHistoryEditor" };
			if( app->QuerySync("role(slug:$slug){id}", {{"slug", roleSlug}}).empty() ){
				let role = app->QuerySync<jobject>( "createRole( slug:$slug, name:$name ){id}", {{"slug", roleSlug}, {"name", "Gateway history editor"}} );
				let roleId = Json::AsNumber<Access::RolePK::Type>( role.at("id") );
				app->QuerySync<jvalue>( "addRole( id:$roleId, permissionRight:{allowed:$allowed, denied:0, resource:{schemaName:$schema, slug:\"nodeIds\"}} )", {{"roleId", roleId}, {"allowed", underlying(Access::ERights::All)}, {"schema", schema}} );
				app->QuerySync<jvalue>( "createAcl( identity:{ id:$userId }, role:{id:$roleId} )", {{"userId", AppClient()->UserPK().Value}, {"roleId", roleId}} );
			}
			app->QuerySync<jvalue>( "restoreResource( schemaName:$schema, slug:$slug, criteria:null )", nodeSlug );
			reached( true, "restore" );
			authorizer.AssignRights( Server::GetUAServer() );
			THROW_IF( empty(authorizer.EditRights(Node(Rpm3), AppClient()->UserPK()) & Access::ERights::Update), "The gateway's user can't edit OpcServer's history." );
		}
		Ω When( uint second )ι->TimePoint{ return _editDay+seconds{ second }; }
		Ω Time( TimePoint t )ι->jvalue{ return UADateTime{ t }.ToJson(); }
		Ω Edit( string q, jobject vars )ε->Edited{
			vars["opc"] = OpcServerSlug;
			let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query(move(q), move(vars), true) );
			TRACET( ELogTags::Test, "edit: {}", serialize(value) );
			let& o = value.as_object();
			Edited y;
			for( let& j : Json::FindDefaultArray(o, "values") )
				y.Values.push_back( Json::AsNumber<StatusCode>(j.as_object(), "status") );
			for( let& j : Json::AsArray(o, "nodes") )
				y.Statuses.emplace( NodeId{Json::AsObject(j.as_object(), "node")}, Json::AsNumber<StatusCode>(j.as_object(), "status") );
			return y;
		}
		//createHistory, updateHistory or upsertHistory.
		Ω Update( sv command, const vector<Sample>& samples )ε->Edited{
			jarray values;
			for( let& sample : samples )
				values.push_back( jobject{ {"node", Node(sample.Node).ToJson()}, {"source", Time(sample.Source)}, {"value", sample.Value} } );
			return Edit( Ƒ("{}( opc: $opc, values: $values ){{ values{{ node source status }} nodes{{ node status }} }}", command), {{"values", move(values)}} );
		}
		Ω DeleteRaw( UA_UInt32 node, TimePoint start, TimePoint end )ε->Edited{
			return Edit( "purgeHistory( opc: $opc, nodes: $nodes, start: $start, end: $end ){ nodes{ node status } }", {{"nodes", Node(node).ToJson()}, {"start", Time(start)}, {"end", Time(end)}} );
		}
		Ω DeleteAtTime( UA_UInt32 node, const vector<TimePoint>& times )ε->Edited{
			jarray j;
			for( let t : times )
				j.push_back( Time(t) );
			return Edit( "purgeHistory( opc: $opc, nodes: $nodes, times: $times ){ values{ node source status } nodes{ node status } }", {{"nodes", Node(node).ToJson()}, {"times", move(j)}} );
		}
		Ω Values( const vector<Row>& rows )ι->vector<jvalue>{
			vector<jvalue> y;
			for( let& row : rows )
				y.push_back( row.Value );
			return y;
		}
		//Every page of a modified read, `limit` a page.
		Ω ReadModified( UA_UInt32 node, TimePoint start, TimePoint end, uint limit )ε->vector<Modified>{
			vector<Modified> y;
			jvalue continuation;
			for( uint i=0; i<100; ++i ){
				jobject vars{ {"opc", OpcServerSlug}, {"nodes", Node(node).ToJson()}, {"start", Time(start)}, {"end", Time(end)}, {"limit", limit}, {"continuation", continuation} };
				let q = "history( opc: $opc, nodes: $nodes, start: $start, end: $end, modified: true, limit: $limit, continuation: $continuation ){ continuation values{ node source value modification{ time type user } } }";
				let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query(q, vars, true) );
				TRACET( ELogTags::Test, "modified: {}", serialize(value) );
				let& o = value.as_object();
				for( let& j : Json::AsArray(o, "values") ){
					let& row = j.as_object();
					let& m = Json::AsObject( row, "modification" );
					y.push_back( {NodeId{Json::AsObject(row, "node")}, UADateTime{row.at("source")}.Time(), row.at("value"), Json::AsString(m, "type"), Json::AsString(m, "user"), UADateTime{m.at("time")}.Time()} );
				}
				continuation = o.contains( "continuation" ) ? o.at( "continuation" ) : jvalue{};
				if( !continuation.is_string() )
					return y;
			}
			THROW( "100 pages and still a continuation." );
		}
		static inline TimePoint _editDay{};
	};

	//Every edit type over the caller's session, each value answered with the server's operation result, and the history
	//read back raw and as modified values, by time and paged.  DeleteAtTime goes to the server too, which refuses it,
	//OpcServer having no callback for it, so each time answers the server's refusal of the node's entry.
	TEST_F( HistEditTests, EditsEveryTypeAndReadsItBackModified ){
		Write( Rpm3, 100, When(40) );
		Write( Rpm3, 200, When(42) );
		Write( Rpm3, 300, When(44) );
		THROW_IF( !BlockAny(Server::GetUAServer().History().Group()->Flush()), "The flush didn't write all it took." );
		let node = Node( Rpm3 );
		let inserted = Update( "createHistory", {{Rpm3, When(41), 150}, {Rpm3, When(40), 1}} );
		EXPECT_EQ( inserted.Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED, UA_STATUSCODE_BADENTRYEXISTS}) );
		EXPECT_EQ( inserted.Statuses.at(node), UA_STATUSCODE_GOOD );
		EXPECT_EQ( Update("updateHistory", {{Rpm3, When(42), 250}, {Rpm3, When(43), 9}}).Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED, UA_STATUSCODE_BADNOENTRYEXISTS}) );
		EXPECT_EQ( Update("upsertHistory", {{Rpm3, When(44), 350}, {Rpm3, When(46), 400}}).Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYREPLACED, UA_STATUSCODE_GOODENTRYINSERTED}) );
		const Request range{ .Nodes={Rpm3}, .Start=When(40), .End=When(47), .Limit=100 };
		EXPECT_EQ( Values(Read(range).Values), (vector<jvalue>{100.0, 150.0, 250.0, 350.0, 400.0}) );

		let deleted = DeleteRaw( Rpm3, When(41), When(44) );//a UA range leaves out its end.
		EXPECT_TRUE( deleted.Values.empty() );
		EXPECT_EQ( deleted.Statuses.at(node), UA_STATUSCODE_GOOD );
		EXPECT_EQ( Values(Read(range).Values), (vector<jvalue>{100.0, 350.0, 400.0}) );
		EXPECT_EQ( DeleteRaw(Rpm3, When(41), When(44)).Statuses.at(node), UA_STATUSCODE_BADNODATA );

		let atTime = DeleteAtTime( Rpm3, {When(46), When(44)} );
		EXPECT_EQ( atTime.Values, (vector<StatusCode>{UA_STATUSCODE_BADNOTSUPPORTED, UA_STATUSCODE_BADNOTSUPPORTED}) );
		EXPECT_EQ( atTime.Statuses.at(node), UA_STATUSCODE_BADNOTSUPPORTED );
		EXPECT_EQ( Values(Read(range).Values), (vector<jvalue>{100.0, 350.0, 400.0}) );

		//An INSERT's modified value is the value inserted, the others' the one replaced or deleted, by time, and at one
		//time in the order the edits were made.
		let user = Server::GetSchema().Authorizer->UserName( AppClient()->UserPK() );
		let earliest = Clock::now()-1min;
		for( let limit : {100u, 2u} ){
			SCOPED_TRACE( Ƒ("limit {}", limit) );
			let modified = ReadModified( Rpm3, When(40), When(47), limit );
			vector<jvalue> values; vector<string> types; vector<TimePoint> sources;
			for( let& m : modified ){
				values.push_back( m.Value );
				types.push_back( m.Type );
				sources.push_back( m.Source );
				EXPECT_EQ( m.Node, node );
				EXPECT_EQ( m.User, user );
				EXPECT_GE( m.Time, earliest );
				EXPECT_LE( m.Time, Clock::now() );
			}
			EXPECT_EQ( values, (vector<jvalue>{150.0, 150.0, 200.0, 250.0, 300.0, 400.0}) );
			EXPECT_EQ( types, (vector<string>{"Insert", "Delete", "Replace", "Delete", "Update", "Update"}) );
			EXPECT_EQ( sources, (vector<TimePoint>{When(41), When(41), When(42), When(42), When(44), When(46)}) );
		}
		let reversed = ReadModified( Rpm3, When(47), When(40), 2 );
		ASSERT_EQ( reversed.size(), 6u );
		EXPECT_EQ( reversed.front().Value, 400.0 );
		EXPECT_EQ( reversed.back().Source, When(41) );
	}

	//A value takes its node's DataType, so a Boolean node takes a Boolean, and the nodes of one call answer each on its own:
	//a node the server doesn't historize with the server's refusal of its entry, one the server doesn't have with its
	//answer to the DataType read.  A value's status goes as given.  Arguments the call can't take, a status that isn't a
	//number among them, are refused before anything is sent.
	TEST_F( HistEditTests, AnswersEachNodeWithTheServersStatus ){
		let edited = Update( "createHistory", {{Status1, When(10), true}, {Status2, When(10), true}, {Unknown, When(10), 1}, {Status1, When(11), false}} );
		EXPECT_EQ( edited.Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED, UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED, UA_STATUSCODE_BADNODEIDUNKNOWN, UA_STATUSCODE_GOODENTRYINSERTED}) );
		EXPECT_EQ( edited.Statuses.at(Node(Status1)), UA_STATUSCODE_GOOD );
		EXPECT_EQ( edited.Statuses.at(Node(Status2)), UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );
		EXPECT_EQ( edited.Statuses.at(Node(Unknown)), UA_STATUSCODE_BADNODEIDUNKNOWN );
		EXPECT_EQ( Values(Read({.Nodes={Status1}, .Start=When(10), .End=When(12), .Limit=100}).Values), (vector<jvalue>{true, false}) );
		EXPECT_EQ( DeleteRaw(Status2, When(10), When(12)).Statuses.at(Node(Status2)), UA_STATUSCODE_BADHISTORYOPERATIONUNSUPPORTED );
		//A value's status goes as given, a number as a read answers it.
		let uncertain = jobject{ {"node", Node(Rpm3).ToJson()}, {"source", Time(When(22))}, {"status", UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE}, {"value", 2} };
		EXPECT_EQ( Edit("createHistory( opc: $opc, values: $values ){ values{ status } nodes{ node status } }", {{"values", jarray{uncertain}}}).Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		let read = Read( {.Nodes={Rpm3}, .Start=When(22), .End=When(23), .Limit=100} ).Values;
		ASSERT_EQ( read.size(), 1u );
		EXPECT_EQ( read[0].Status, UA_STATUSCODE_UNCERTAINLASTUSABLEVALUE );

		let refused = []( string q, jobject vars ){ EXPECT_THROW( Edit(q, vars), GatewayErrorResponse ) << q; };
		jarray values{ jobject{ {"node", Node(Rpm3).ToJson()}, {"source", Time(When(20))}, {"value", 1} } };
		refused( "createHistory( opc: $opc, group: 1, values: $values ){ values{ status } }", {{"values", values}} );
		refused( "createHistory( opc: $opc, values: $values ){ values{ status } }", {{"values", jarray{}}} );
		refused( "createHistory( opc: $opc, values: $values ){ values{ status } }", {{"values", jarray{jobject{{"source", Time(When(20))}, {"value", 1}}}}} );
		try{//a value that can't take its node's type refuses the call, naming the node and the value.
			Edit( "createHistory( opc: $opc, values: $values ){ values{ status } nodes{ node status } }", {{"values", jarray{jobject{{"node", Node(Rpm3).ToJson()}, {"source", Time(When(20))}, {"value", "not a number"}}}}} );
			ADD_FAILURE() << "a value that can't be a Double was taken";
		}
		catch( const GatewayErrorResponse& e ){
			EXPECT_NE( string{e.what()}.find(Ƒ("createHistory:  {}'s value \"not a number\" isn't a Double", Node(Rpm3).ToString())), string::npos ) << e.what();
		}
		refused( "createHistory( opc: $opc, values: $values ){ values{ status } }", {{"values", jarray{jobject{{"node", Node(Rpm3).ToJson()}, {"source", Time(When(20))}, {"status", "0x80340000"}, {"value", 1}}}}} );
		refused( "purgeHistory( opc: $opc, nodes: $nodes, start: $start ){ nodes{ status } }", {{"nodes", Node(Rpm3).ToJson()}, {"start", Time(When(20))}} );
		refused( "purgeHistory( opc: $opc, nodes: $nodes, times: $times ){ nodes{ status } }", {{"nodes", Node(Rpm3).ToJson()}, {"times", jarray{}}} );
		refused( "purgeHistory( opc: $opc, nodes: $nodes, start: $start, end: $end, times: $times ){ nodes{ status } }", {{"nodes", Node(Rpm3).ToJson()}, {"start", Time(When(20))}, {"end", Time(When(21))}, {"times", jarray{Time(When(20))}}} );
		EXPECT_TRUE( Read({.Nodes={Rpm3}, .Start=When(20), .End=When(21), .Limit=100}).Values.empty() );
	}

	//A range purge's range is the server's (spec *Pass-through*):  OpcServer leaves out its end, and takes `start` equal to
	//`end` as the one value there, as a group does.
	TEST_F( HistEditTests, PurgesOneValueWithStartEqualToEnd ){
		EXPECT_EQ( Update("createHistory", {{Rpm3, When(70), 1}, {Rpm3, When(71), 2}}).Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED, UA_STATUSCODE_GOODENTRYINSERTED}) );
		EXPECT_EQ( DeleteRaw(Rpm3, When(70), When(70)).Statuses.at(Node(Rpm3)), UA_STATUSCODE_GOOD );
		EXPECT_EQ( Values(Read({.Nodes={Rpm3}, .Start=When(70), .End=When(72), .Limit=100}).Values), (vector<jvalue>{2.0}) );
	}

	//A call that names both `opc` and `group`, or neither, is refused as such, before a client is opened for it:  an `opc`
	//that names no connection gets the same answer, and a null `opc` names nothing.  `group` alone is a group's (Phase 5).
	TEST_F( HistEditTests, RefusesBothOrNeitherOfOpcAndGroup ){
		jarray values{ jobject{ {"node", Node(Rpm3).ToJson()}, {"source", Time(When(25))}, {"value", 1} } };
		struct Case final{ sv Command; string Query; jobject Vars; };
		for( let& c : vector<Case>{
			{"createHistory", "createHistory( opc: $opc, group: 1, values: $values ){ values{ status } }", {{"opc", "noSuchConnection"}, {"values", values}}},
			{"createHistory", "createHistory( opc: $opc, values: $values ){ values{ status } }", {{"opc", nullptr}, {"values", values}}},
			{"createHistory", "createHistory( values: $values ){ values{ status } }", {{"values", values}}},
			{"purgeHistory", "purgeHistory( nodes: $nodes, start: $start, end: $end ){ nodes{ status } }", {{"nodes", Node(Rpm3).ToJson()}, {"start", Time(When(25))}, {"end", Time(When(26))}}} } ){
			try{
				Socket().QuerySync( string{c.Query}, c.Vars );
				ADD_FAILURE() << c.Query;
			}
			catch( const GatewayErrorResponse& e ){
				EXPECT_NE( string{e.what()}.find(Ƒ("{} takes exactly one of 'opc' and 'group'.", c.Command)), string::npos ) << c.Query << ":  " << e.what();
			}
		}
		EXPECT_TRUE( Read({.Nodes={Rpm3}, .Start=When(25), .End=When(26), .Limit=100}).Values.empty() );
	}

	//A DataType the gateway has no built-in type for, here the abstract Number, takes the type of the node's value though
	//its status is Bad, and with no value each value takes the type its json implies:  either way the edit goes to the
	//server, which judges it.  7 goes as the live value's Double, so it reads back 7.0;  8.5 as the json's.
	TEST_F( HistEditTests, TypesAValueByTheNodesValueOrItsJson ){
		let ua = Server::GetUAServer().Ptr();
		let node = Node( Rpm2 );
		UA_ReadValueId id; UA_ReadValueId_init( &id );
		id.nodeId = node;
		id.attributeId = UA_ATTRIBUTEID_VALUE;
		const Value original{ UA_Server_read(ua, &id, UA_TIMESTAMPSTORETURN_NEITHER) };
		UAε( UA_Server_writeDataType(ua, node, UA_NODEID_NUMERIC(0, UA_NS0ID_NUMBER)) );
		absl::Cleanup restore = [&]{//the value first:  Number takes an empty one, Double doesn't.
			EXPECT_EQ( UA_Server_writeDataValue(ua, node, original), UA_STATUSCODE_GOOD );
			EXPECT_EQ( UA_Server_writeDataType(ua, node, UA_NODEID_NUMERIC(0, UA_NS0ID_DOUBLE)), UA_STATUSCODE_GOOD );
		};
		Value down{ UA_STATUSCODE_BADCOMMUNICATIONERROR };
		const UA_Double last{ 3 };
		UAε( UA_Variant_setScalarCopy(&down.value, &last, &UA_TYPES[UA_TYPES_DOUBLE]) );
		down.hasValue = true;
		UAε( UA_Server_writeDataValue(ua, node, down) );
		let typed = Update( "createHistory", {{Rpm2, When(60), 7}} );
		EXPECT_EQ( typed.Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		EXPECT_EQ( typed.Statuses.at(node), UA_STATUSCODE_GOOD );

		UAε( UA_Server_writeDataValue(ua, node, Value{UA_STATUSCODE_BADWAITINGFORINITIALDATA}) );
		let inferred = Update( "createHistory", {{Rpm2, When(61), 8.5}} );
		EXPECT_EQ( inferred.Values, (vector<StatusCode>{UA_STATUSCODE_GOODENTRYINSERTED}) );
		EXPECT_EQ( inferred.Statuses.at(node), UA_STATUSCODE_GOOD );
		EXPECT_EQ( Values(Read({.Nodes={Rpm2}, .Start=When(60), .End=When(62), .Limit=100}).Values), (vector<jvalue>{7.0, 8.5}) );
	}

	//The server's access control decides, never the gateway's:  with nothing enforcing nodeIds, OpcServer refuses every
	//edit, whatever the gateway's user may do elsewhere, and each value answers that refusal.
	TEST_F( HistEditTests, EditsNeedTheServersGrant ){
		Enforce( false );
		absl::Cleanup enforce = []{ try{ Enforce(true); }catch( const std::exception& e ){ ADD_FAILURE() << "nodeIds wasn't enforced again:  " << e.what(); } };
		let edited = Update( "createHistory", {{Rpm3, When(30), 500}} );
		EXPECT_EQ( edited.Values, (vector<StatusCode>{UA_STATUSCODE_BADUSERACCESSDENIED}) );
		EXPECT_EQ( DeleteRaw(Rpm3, When(0), When(50)).Statuses.at(Node(Rpm3)), UA_STATUSCODE_BADUSERACCESSDENIED );
		EXPECT_TRUE( Read({.Nodes={Rpm3}, .Start=When(30), .End=When(31), .Limit=100}).Values.empty() );
	}
}