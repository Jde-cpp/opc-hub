//Historian 3A (#214):  hist with `opc`, the gateway reading a server's own history for the caller (spec *Pass-through*)
//- here the embedded OpcServer's pump nodes, written on the server with source times on three past days, so a read
//pages across its day files, and read back through the gateway's QL by time, as the web will.
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/settings.h>
#include <jde/historian/Historian.h>
#include <jde/opc/uatypes/DateTime.h>
#include "utils/GatewayClientSocket.h"
#include "utils/helpers.h"
#include "../src/UAClient.h"
#include "../../OpcServer/src/globals.h"
#include "../../OpcServer/src/UAServer.h"
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
			let q = "hist( opc: $opc, nodes: $nodes, start: $start, end: $end, limit: $limit, returnBounds: $bounds, continuation: $continuation ){ continuation values{ node source server status value bound } nodes{ node status } }";
			let value = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query(q, vars, true) );
			TRACET( ELogTags::Test, "hist: {}", serialize(value) );
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
		EXPECT_THROW( (BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("hist( opc: $opc, group: 1, nodes: $node, start: $start, end: $end ){ values{ value } }", vars, true) )), GatewayErrorResponse );
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
		let modified = BlockAwait<Web::Client::ClientSocketAwait<jvalue>,jvalue>( Socket().Query("hist( opc: $opc, nodes: $node, start: $start, end: $end, modified: true ){ values{ value } nodes{ node status } }", vars, true) );
		let& o = modified.as_object();
		EXPECT_TRUE( Json::AsArray(o, "values").empty() );
		let& nodes = Json::AsArray( o, "nodes" );
		ASSERT_EQ( nodes.size(), 1u );
		EXPECT_EQ( Json::AsNumber<StatusCode>(nodes[0].as_object(), "status"), UA_STATUSCODE_GOODNODATA );
	}
}