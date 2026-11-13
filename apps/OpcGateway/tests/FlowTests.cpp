//Flows 2 (#448):  the host (docs/flows/spec.md *Flows*, *Rights*, *Context*) - the four tables, each edit's live side
//(FlowMAwait), deployFlow, injectFlow and setFlowContext, the admission of a graph for its deployer at the deploy and again
//as the rights and the groups change, and the context kept across a restart - over the embedded OpcServer's pumps.
//Flows 3 (#449):  the sources and sinks (*Sources*, *Sinks*) - `opc in`, `group in`, `timer`, `calendar`, `connection` and a
//link from another flow, and `opc out`, `log` and `http request` - each run in a deployed flow, what it sent read from the
//counts and from the lines a `log` node wrote.
#include <fstream>
#include <thread>
#include <absl/cleanup/cleanup.h>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <jde/tests/Capture.h>
#include <jde/db/meta/AppSchema.h>
#include <jde/calendar/client/Calendars.h>
#include "utils/HistGroupFixture.h"
#include "utils/Subscribing.h"
#include "../src/flows/FlowHost.h"
#include "../../OpcServer/src/access/OpcAuthorize.h"
#include "../../../libs/historian/tests/ManualClock.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using Access::ERights;
	//The `http request` cases' own web server, on a loopback port the system picks:  `/ok` answers 200 with the request's
	//method and body as json, and `/redirect?to=<url>` a 302 to the url.  One request a connection, as the client makes them.
	struct TestWeb final{
		TestWeb()ε:
			_acceptor{ _ioc, Endpoint(0) },
			_port{ _acceptor.local_endpoint().port() },
			_thread{ [this]{ Serve(); } }
		{}
		~TestWeb(){
			_stopped = true;
			try{
				boost::asio::ip::tcp::socket wake{ _ioc };
				wake.connect( Endpoint(_port) );
			}
			catch( const std::exception& )
			{}
			_thread.join();
		}
		α Url( sv host, sv target )Ι->string{ return Ƒ( "http://{}:{}{}", host, _port, target ); }
		std::atomic<uint> Requests{};
	private:
		Ω Endpoint( PortType port )ι->boost::asio::ip::tcp::endpoint{ return { boost::asio::ip::make_address_v4("127.0.0.1"), port }; }
		α Serve()ι->void{
			namespace http = boost::beast::http;
			for( ;; ){
				boost::beast::error_code ec;
				boost::asio::ip::tcp::socket socket{ _ioc };
				_acceptor.accept( socket, ec );
				if( _stopped || ec )
					return;
				boost::beast::flat_buffer buffer;
				http::request<http::string_body> req;
				http::read( socket, buffer, req, ec );
				if( ec )
					continue;
				++Requests;
				const sv target{ req.target() };
				http::response<http::string_body> res{ http::status::ok, req.version() };
				if( let to = target.find("to="); target.starts_with("/redirect") && to!=sv::npos ){
					res.result( http::status::found );
					res.set( http::field::location, target.substr(to+3) );
				}
				else if( target.starts_with("/ok") ){
					res.set( http::field::content_type, "application/json" );
					res.body() = serialize( jobject{{"ok", true}, {"method", sv{req.method_string()}}, {"body", req.body()}} );
				}
				else
					res.result( http::status::not_found );
				res.keep_alive( false );
				res.prepare_payload();
				http::write( socket, res, ec );
				socket.shutdown( boost::asio::ip::tcp::socket::shutdown_both, ec );
			}
		}
		boost::asio::io_context _ioc;
		boost::asio::ip::tcp::acceptor _acceptor;
		const PortType _port;
		std::atomic<bool> _stopped{};
		std::thread _thread;
	};

	struct FlowTests : HistGroupFixture{
	protected:
		α SetUp()->void override{
			HistGroupFixture::SetUp();
			if( !IsSkipped() && !Flows().Enabled() )
				GTEST_SKIP() << "The gateway runs no flows:  /gateway/flows.";
		}
		Ω Flows()ι->FlowHost&{ return FlowHost::Instance(); }
		//A node of a graph:  its props as json text, its wires a list of target ids per output port.
		Ω N( sv id, sv type, sv props="{}", std::initializer_list<std::initializer_list<sv>> wires={} )->jobject{
			jarray ports;
			for( let& port : wires ){
				jarray ids;
				for( let target : port )
					ids.emplace_back( target );
				ports.push_back( move(ids) );
			}
			return { {"id", id}, {"type", type}, {"props", parse(props)}, {"wires", move(ports)} };
		}
		Ω Graph( std::initializer_list<jobject> nodes )->jvalue{
			jarray y;
			for( let& node : nodes )
				y.push_back( node );
			return jobject{ {"nodes", move(y)} };
		}
		//An inject into a debug, and an inject into a hosted node of `type` on `node` of the suite's connection.
		Ω Plain()->jvalue{ return Graph( {N("in", "inject", "{}", {{"d"}}), N("d", "debug")} ); }
		Ω On( const jvalue& node )->string{ return serialize( jobject{{"connection", OpcServerSlug}, {"node", node}} ); }
		Ω Writes( const jvalue& node )->jvalue{ return Graph( {N("in", "inject", "{}", {{"w"}}), N("w", "opc out", On(node))} ); }
		Ω Reads( const jvalue& node )->jvalue{ return Graph( {N("src", "opc in", On(node), {{"d"}}), N("d", "debug")} ); }
		//A `log` node, `line`, and what it wrote since `capture` began, in order, each less its `[flow.node]` head.
		Ω Line( sv text )->jobject{ return N( "line", "log", serialize(jobject{{"template", text}}) ); }
		Ω Lines( const Logging::Capture& capture, sv flow )ι->vector<string>{
			let head = Ƒ( "[{}.line]", flow );
			vector<string> y;
			for( let& entry : capture.Find([&]( const Logging::Entry& e ){ return e.Message().starts_with( head ); }) )
				y.push_back( entry.Message().substr(head.size()) );
			return y;
		}
		Ω Errors( uint32 flow, sv node, uint count )ι->bool{ return WaitUntil( [&]{ return Counts( flow, node ).Errors==count; } ); }
		//The flows on a clock the test moves, until the cleanup puts them back on the system's, for the tests after.
		Ω OnClock( TimePoint start )ε{
			auto clock = ms<Hist::Tests::ManualClock>( start );
			Flows().Stop();
			Flows().Start( clock );
			return std::pair{ clock, absl::Cleanup{ []{ Flows().Stop(); Flows().Start(); } } };
		}
		//The hosts an `http request` may reach, until the cleanup, which leaves none.
		Ω Allow( jarray hosts )ε->void{ Settings::Set( "/gateway/flows/http/allow", move(hosts) ); }

		Ω CreateFlow( sv slug, bool enabled=true )ε->uint32{
			return Id( Mutate("createFlow( name:$name, slug:$slug, enabled:$enabled ){ id }", {{"name", Ƒ("Flow {}", slug)}, {"slug", slug}, {"enabled", enabled}}) );
		}
		Ω PurgeFlow( uint32 flow )ι->void{
			try{
				Mutate( "purgeFlow( id:$id )", {{"id", flow}} );
			}
			catch( const std::exception& e ){
				WARNT( ELogTags::Test, "purgeFlow({}):  {}", flow, e.what() );
			}
		}
		static constexpr sv DeployQL{ "deployFlow( flow:$flow, graph:$graph, comment:$comment ){ flow version userName running dropped fault{ node message } }" };
		Ω Deploy( uint32 flow, const jvalue& graph, sv comment={} )ε->jobject{
			return Mutate( DeployQL, {{"flow", flow}, {"graph", graph}, {"comment", comment.empty() ? jvalue{} : jvalue{comment}}} ).as_object();
		}
		//Through the gateway's socket, so under the suite's own web session, which an ungrouped `opc in` is read with.
		Ω DeployBySession( uint32 flow, const jvalue& graph )ε->jobject{
			return Socket().QuerySync( string{DeployQL}, {{"flow", flow}, {"graph", graph}, {"comment", nullptr}} ).as_object();
		}
		Ω Inject( uint32 flow, sv node="in", jvalue message={} )ε->void{
			Mutate( "injectFlow( flow:$flow, node:$node, message:$message ){ node }", {{"flow", flow}, {"node", node}, {"message", move(message)}} );
		}
		//What f's refusal said and its status;  none when it didn't throw.
		struct Refusal final{ EHttpStatus Status{ EHttpStatus::None }; string What; };
		Ω Refused( function<void()> f )->Refusal{
			try{
				f();
			}
			catch( const Exception& e ){
				return { e.HttpStatus(), e.what() };
			}
			return {};
		}
		Ω Has( str text, sv part )ι->::testing::AssertionResult{
			return text.find( part )!=string::npos ? ::testing::AssertionSuccess() : ::testing::AssertionFailure() << "'" << text << "' lacks '" << part << "'";
		}
		Ω Running( uint32 flow )ι->bool{ let state = Flows().Find( flow ); return state && state->Running; }
		Ω Fault( uint32 flow )ι->Flows::Fault{ let state = Flows().Find( flow ); return state && state->Fault ? *state->Fault : Flows::Fault{}; }
		Ω Counts( uint32 flow, sv node )ι->Flows::NodeCounts{
			for( auto& counts : Flows().Counts(flow) ){
				if( counts.Node==node )
					return counts;
			}
			return {};
		}
		Ω Received( uint32 flow, sv node, uint count )ι->bool{ return WaitUntil( [&]{ return Counts( flow, node ).Received==count; } ); }
		//What the node has sent from each port, once it is `sent`:  a message is counted in before the node sends it on.
		Ω Sent( uint32 flow, sv node, const vector<uint>& sent )ι->vector<uint>{
			WaitUntil( [&]{ return Counts( flow, node ).Sent==sent; } );
			return Counts( flow, node ).Sent;
		}
		Ω Versions( uint32 flow )ε->jarray{ return QL().QuerySync<jarray>( "flowVersions( flowId:$flow ){ version graph identityId userName comment }", {{"flow", flow}}, User() ); }
		Ω Contexts( uint32 flow )ε->jarray{ return QL().QuerySync<jarray>( "flowContexts( flowId:$flow ){ nodeId name value }", {{"flow", flow}}, User() ); }
		//setFlowContext:  a null flow is the gateway's scope, a null node the flow's.
		Ω SetContext( const jvalue& flow, const jvalue& node, sv name, const jvalue& value )ε->jobject{
			return Mutate( "setFlowContext( flow:$flow, node:$node, name:$name, value:$value ){ flow node name value }", {{"flow", flow}, {"node", node}, {"name", name}, {"value", value}} ).as_object();
		}
		//The row's value, compared as C++ compares:  none with no row.
		Ω Held( uint32 flow, sv node, sv name )ε->string{
			for( let& row : Contexts(flow) ){
				if( let& o = row.as_object(); Json::AsSV(o, "name")==name && Json::FindDefaultSV(o, "nodeId")==node )
					return Json::AsString( o, "value" );
			}
			return {};
		}
		Ω Execute( string sql )ε->void{ DS()->ExecuteSync( {move(sql), {}} ); }//mysql's text protocol, with no params:  a trigger can't be prepared.
		//The table under another name until the cleanup, so each statement that names it fails.
		Ω Renamed( sv table )ε{
			let name = GetViewPtr( table )->SqlName();
			Execute( Ƒ("alter table {} rename to {}_away", name, name) );
			return absl::Cleanup{ [name]{ try{ Execute( Ƒ("alter table {}_away rename to {}", name, name) ); }catch( const std::exception& e ){ ADD_FAILURE() << name << " wasn't named again:  " << e.what(); } } };
		}
		//A trigger that fails each insert of the context key `name` until the cleanup:  sqlite's or mysql's, the dialects the suite runs on.
		Ω FailingInserts( sv name )ε{
			let table = GetViewPtr( FlowTable::Contexts )->SqlName();
			let& syntax = DS()->Syntax();
			THROW_IF( syntax.HasProcs() && !syntax.HasUnsigned(), "No trigger is written here for SQL Server." );
			Execute( syntax.HasProcs()
				? Ƒ( "create trigger flow_tests_fail before insert on {} for each row begin if new.name='{}' then signal sqlstate '45000' set message_text='refused by the test'; end if; end", table, name )
				: Ƒ( "create trigger flow_tests_fail before insert on {} when new.name='{}' begin select raise(abort, 'refused by the test'); end", table, name ) );
			return absl::Cleanup{ []{ try{ Execute( "drop trigger flow_tests_fail" ); }catch( const std::exception& e ){ ADD_FAILURE() << "the trigger wasn't dropped:  " << e.what(); } } };
		}
		Ω Row( uint32 flow )ε->jobject{
			let rows = QL().QuerySync<jarray>( "flows( id:$id ){ id guid version enabled deleted }", {{"id", flow}}, User() );
			THROW_IF( rows.size()!=1, "Flow {} has {} rows.", flow, rows.size() );
			return rows[0].as_object();
		}

		//The flows resource enforced, with the suite's user granted everything on it through a role of its own, or soft-deleted
		//again, as it ships;  and a flow's own row, its guid the criteria, enforced with `allowed`, which stands in for the
		//root for that flow alone - as HistGroupFixture's two for a group.
		Ω EnforceFlows( bool on )ε->void{
			EnforceResource( "gateway", "flows", false );
			if( !on )
				return;
			auto app = AppClient();
			GrantRole( *app, "GatewayFlowEditor", "gateway", "flows", {}, ERights::All, app->UserPK() );
			EnforceResource( "gateway", "flows", true );
		}
		Ω EnforcedFlows()ε{
			absl::Cleanup open = []{ try{ EnforceFlows( false ); }catch( const std::exception& e ){ ADD_FAILURE() << "flows wasn't opened again:  " << e.what(); } };
			EnforceFlows( true );
			return open;
		}
		Ω ScopeFlow( uint32 flow, ERights allowed )ε->void{
			let app = AppClient();
			let criteria = Flows().Find( flow )->Row.Guid;
			app->QuerySync<jvalue>( "createResource( schemaName:\"gateway\", name:$name, slug:\"flows\", criteria:$criteria, allowed:255 )", {{"name", Ƒ("flows[{}]", flow)}, {"criteria", criteria}} );
			GrantRole( *app, Ƒ("GatewayFlow{}", flow), "gateway", "flows", criteria, allowed | ERights::Administer, app->UserPK() );
			EnforceResource( "gateway", "flows", true, criteria );//a row is created unenforced.
		}
		Ω UnscopeFlow( uint32 flow )ε->void{ EnforceResource( "gateway", "flows", false, Flows().Find(flow)->Row.Guid ); }
	};

	TEST_F( FlowTests, ADeployIsAVersion ){
		let created = Mutate( "createFlow( name:$name, slug:$slug ){ id guid enabled version }", {{"name", "Deployed"}, {"slug", "deployed"}} ).as_object();
		let flow = Id( created );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		EXPECT_EQ( Json::AsString(created, "guid").size(), 36u ) << serialize( created );
		ASSERT_TRUE( Flows().Find(flow).has_value() ) << "the host holds the row from its create";
		EXPECT_FALSE( Running(flow) ) << "a flow with no version has nothing to run";
		EXPECT_TRUE( Row(flow).at("version").is_null() );

		let graph = Plain();
		let first = Deploy( flow, graph, "the first" );
		EXPECT_EQ( first.at("version"), 1 ) << serialize( first );
		EXPECT_EQ( first.at("running"), true );
		EXPECT_TRUE( first.at("fault").is_null() );
		EXPECT_EQ( first.at("userName"), jvalue{AppClient()->Acl()->UserName(User())} );
		EXPECT_EQ( Row(flow).at("version"), 1 );
		let versions = Versions( flow );
		ASSERT_EQ( versions.size(), 1u ) << serialize( versions );
		let& version = versions[0].as_object();
		EXPECT_EQ( parse(Json::AsString(version, "graph")), graph );
		EXPECT_EQ( version.at("identityId").to_number<uint>(), User().Value );
		EXPECT_EQ( version.at("comment"), "the first" );
		Inject( flow );
		EXPECT_TRUE( Received(flow, "d", 1) ) << "the version runs";

		let second = Deploy( flow, graph );
		EXPECT_EQ( second.at("version"), 2 );
		EXPECT_EQ( Row(flow).at("version"), 2 );
		EXPECT_EQ( Versions(flow).size(), 2u );

		let forged = Refused( [&]{ Mutate( "createFlowVersion( flowId:$flow, version:9, graph:\"{}\", identityId:1, userName:\"root\" ){ id }", {{"flow", flow}} ); } );
		EXPECT_EQ( forged.Status, EHttpStatus::BadRequest );
		EXPECT_TRUE( Has(forged.What, "written by deployFlow alone") );
		EXPECT_TRUE( Has(Refused([&]{ Mutate("purgeFlowContext( id:1 )"); }).What, "the flows' own") );
		EXPECT_TRUE( Has(Refused([&]{ Mutate("purgeFlowRun( id:1 )"); }).What, "a replay's to write") );
		EXPECT_THROW( Mutate("updateFlow( id:$id, version:7 )", {{"id", flow}}), std::exception ) << "the version is deployFlow's";
		EXPECT_EQ( Versions(flow).size(), 2u );
	}

	TEST_F( FlowTests, AGraphIsRefusedByItsNode ){
		let flow = CreateFlow( "refused-graph" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let wire = Refused( [&]{ Deploy( flow, Graph({N("in", "inject", "{}", {{"gone"}})}) ); } );
		EXPECT_EQ( wire.Status, EHttpStatus::BadRequest );
		EXPECT_TRUE( wire.What.starts_with("Node 'in':  ") && Has(wire.What, "'gone', which is not a node of the graph") ) << wire.What;
		let props = Refused( [&]{ Deploy( flow, Graph({N("in", "inject", "{}", {{"sw"}}), N("sw", "switch")}) ); } );
		EXPECT_TRUE( props.What.starts_with("Node 'sw':  ") && Has(props.What, "`rules`") ) << props.What;
		let shape = Refused( [&]{ Deploy( flow, jobject{} ); } );
		EXPECT_TRUE( shape.What.starts_with("A graph is an object") ) << "a fault of no node names none:  " << shape.What;
		EXPECT_TRUE( Has(Refused([&]{ Deploy( flow, jvalue{"{nodes"} ); }).What, "graph is a flow's graph") );
		EXPECT_TRUE( Has(Refused([&]{ Deploy( flow, jvalue{serialize(Plain())}, string(2049, 'c') ); }).What, "over the column's 2048") );
		EXPECT_TRUE( Versions(flow).empty() ) << "a refusal writes nothing";
		EXPECT_TRUE( Row(flow).at("version").is_null() );
		EXPECT_FALSE( Running(flow) );

		EXPECT_EQ( Deploy(flow, jvalue{serialize(Plain())}).at("version"), 1 ) << "a graph is taken as its text too";
		EXPECT_EQ( Refused([&]{ Deploy( 4'000'000'000, Plain() ); }).Status, EHttpStatus::NotFound );
		EXPECT_TRUE( Has(Refused([&]{ Mutate("createFlow( name:\"Bad\", slug:\"../bad\" ){ id }"); }).What, "is not valid") ) << "the slug is a directory's name";
		EXPECT_TRUE( Has(Refused([&]{ Mutate("updateFlow( id:$id, trace:\"loud\" )", {{"id", flow}}); }).What, "off, counts, errors or hops") );
	}

	TEST_F( FlowTests, EnabledStartsAndStops ){
		let flow = CreateFlow( "toggled" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		Deploy( flow, Plain() );
		Inject( flow );
		ASSERT_TRUE( Received(flow, "d", 1) );
		let enable = [&]( bool on ){ Mutate( "updateFlow( id:$id, enabled:$enabled )", {{"id", flow}, {"enabled", on}} ); };

		enable( false );
		EXPECT_FALSE( Running(flow) );
		let stopped = Refused( [&]{ Inject( flow ); } );
		EXPECT_EQ( stopped.Status, EHttpStatus::Conflict );
		EXPECT_TRUE( Has(stopped.What, "is not running") );
		enable( true );
		EXPECT_TRUE( Running(flow) ) << "from its current version";
		EXPECT_EQ( Counts(flow, "d").Received, 0u ) << "a start is another run";
		Inject( flow, "in", jobject{{"payload", 5}} );
		EXPECT_TRUE( Received(flow, "d", 1) );
		EXPECT_EQ( Refused([&]{ Inject( flow, "d" ); }).Status, EHttpStatus::BadRequest ) << "a debug is no inject";
		EXPECT_EQ( Refused([&]{ Inject( 4'000'000'000 ); }).Status, EHttpStatus::NotFound );

		Mutate( "deleteFlow( id:$id )", {{"id", flow}} );
		EXPECT_FALSE( Running(flow) ) << "a deleted flow stops";
		EXPECT_TRUE( Has(Refused([&]{ Deploy( flow, Plain() ); }).What, "is deleted") );
		Mutate( "restoreFlow( id:$id )", {{"id", flow}} );
		EXPECT_TRUE( Running(flow) );

		let disabled = CreateFlow( "made-disabled", false );
		absl::Cleanup purgeDisabled = [=]{ PurgeFlow( disabled ); };
		let deployed = Deploy( disabled, Plain() );
		EXPECT_EQ( deployed.at("version"), 1 );
		EXPECT_EQ( deployed.at("running"), false ) << "a disabled flow's deploy is its version alone";
		Mutate( "updateFlow( id:$id, enabled:true )", {{"id", disabled}} );
		EXPECT_TRUE( Running(disabled) );
	}

	TEST_F( FlowTests, ARedeployRestartsTheFlowAlone ){
		let flow = CreateFlow( "redeployed" ), beside = CreateFlow( "beside" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); PurgeFlow( beside ); };
		let held = Graph( {N("in", "inject", "{}", {{"wait"}}), N("wait", "delay", R"({"duration":"PT1H"})", {{"d"}}), N("d", "debug")} );
		Deploy( flow, held );
		Deploy( beside, Plain() );
		for( uint i=0; i<3; ++i )
			Inject( flow );
		Inject( beside );
		ASSERT_TRUE( Received(flow, "wait", 3) );
		ASSERT_TRUE( Received(beside, "d", 1) );

		let again = Deploy( flow, held );
		EXPECT_EQ( again.at("version"), 2 );
		EXPECT_EQ( again.at("dropped"), 3 ) << "what the delay held went with the run it was held in";
		EXPECT_EQ( again.at("running"), true );
		EXPECT_EQ( Counts(flow, "wait").Received, 0u );
		EXPECT_TRUE( Running(beside) );
		EXPECT_EQ( Counts(beside, "d").Received, 1u ) << "the flow beside it ran on";
	}

	TEST_F( FlowTests, APurgeTakesTheFlowsRows ){
		let flow = CreateFlow( "purged-flow" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let guid = Json::AsString( Row(flow), "guid" );
		Deploy( flow, Graph({N("in", "inject", "{}", {{"set"}}), N("set", "change", R"({"rules":[{"t":"set","p":"seen","pt":"flow","to":"payload","tot":"msg"}]})", {{"d"}}), N("d", "debug")}) );
		Inject( flow, "in", jobject{{"payload", 5}} );
		ASSERT_TRUE( WaitUntil([&]{ return !Contexts( flow ).empty(); }) ) << "the context is written a second on";
		let directory = *Settings::FindPath( "/gateway/flows/path" )/"purged-flow";
		fs::create_directories( directory/"2026" );
		std::ofstream{ directory/"2026"/"trace" } << "a day";
		auto reopen = EnforcedFlows();
		ScopeFlow( flow, ERights::All );
		let app = AppClient();
		let resources = [&]{ return app->QuerySync<jarray>( "resources( schemaName:\"gateway\", slug:\"flows\", criteria:$criteria ){ id }", {{"criteria", guid}} ); };
		ASSERT_EQ( resources().size(), 1u );
		let resourceId = Id( resources()[0] );
		let grants = [&]{ return app->QuerySync<jarray>( "permissionRights( resourceId:$id ){ id }", {{"id", resourceId}} ); };
		ASSERT_FALSE( grants().empty() );

		Mutate( "purgeFlow( id:$id )", {{"id", flow}} );
		std::move( purge ).Cancel();
		EXPECT_TRUE( QL().QuerySync<jarray>("flows( id:$id ){ id }", {{"id", flow}}, User()).empty() );
		EXPECT_TRUE( Versions(flow).empty() ) << "a purge takes the versions";
		EXPECT_TRUE( Contexts(flow).empty() ) << "and the context";
		EXPECT_FALSE( fs::exists(directory) ) << "and the trace directory";
		EXPECT_TRUE( resources().empty() ) << "and the flow's access row";
		EXPECT_TRUE( grants().empty() ) << "with the grants on it";
		EXPECT_FALSE( Flows().Find(flow).has_value() );
	}

	TEST_F( FlowTests, AnOpcOutNeedsUpdateOnTheGroupThatHoldsItsNode ){
		let flow = CreateFlow( "writes" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let graph = Writes( HistGroupFixture::Ex(RpmManual) );
		let ungrouped = Refused( [&]{ Deploy( flow, graph ); } );
		EXPECT_EQ( ungrouped.Status, EHttpStatus::Forbidden );
		EXPECT_TRUE( ungrouped.What.starts_with("Node 'w':  ") && Has(ungrouped.What, "is in no group") ) << ungrouped.What;

		let itemsBefore = Items();
		let group = CreateGroup( "flow-writes" );
		absl::Cleanup purgeGroup = [=]{ Purge( group, itemsBefore ); };
		AddNode( group, RpmManual );
		let unenforced = Refused( [&]{ Deploy( flow, graph ); } );
		EXPECT_EQ( unenforced.Status, EHttpStatus::Forbidden );
		EXPECT_TRUE( Has(unenforced.What, "needs Update on group 'Collector flow-writes'") && Has(unenforced.What, "nothing enforces it") ) << unenforced.What;

		auto reopen = EnforcedHistGroups();
		ScopeHistGroup( group, ERights::Read );
		let ungranted = Refused( [&]{ Deploy( flow, graph ); } );
		EXPECT_EQ( ungranted.Status, EHttpStatus::Forbidden );
		EXPECT_TRUE( Has(ungranted.What, "needs Update on group 'Collector flow-writes'") && !Has(ungranted.What, "nothing enforces it") ) << ungranted.What;
		EXPECT_TRUE( Versions(flow).empty() );

		UnscopeHistGroup( group );
		let admitted = Deploy( flow, graph );
		EXPECT_EQ( admitted.at("running"), true ) << serialize( admitted );
		EXPECT_EQ( Deploy(flow, Writes(jvalue{Node(RpmManual).ToString()})).at("running"), true ) << "a node by its index is spelled through the collector";
	}

	TEST_F( FlowTests, AGrantWithdrawnStopsTheFlow ){
		let flow = CreateFlow( "withdrawn" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let itemsBefore = Items();
		let group = CreateGroup( "flow-withdrawn" );
		absl::Cleanup purgeGroup = [=]{ Purge( group, itemsBefore ); };
		AddNode( group, RpmManual );
		auto reopen = EnforcedHistGroups();
		let graph = Writes( HistGroupFixture::Ex(RpmManual) );
		ASSERT_EQ( Deploy(flow, graph).at("running"), true );

		ScopeHistGroup( group, ERights::Read );//the group's own row, which grants the deployer no Update.
		ASSERT_TRUE( WaitUntil([&]{ return !Running( flow ); }, 15s) ) << "the access change is heard";
		let fault = Fault( flow );
		EXPECT_EQ( fault.NodeId, "w" );
		EXPECT_TRUE( Has(fault.Message, "needs Update on group 'Collector flow-withdrawn'") );
		EXPECT_EQ( Flows().Status(flow, "w"), (Flows::NodeStatus{"red", "ring", fault.Message}) );
		EXPECT_EQ( Row(flow).at("enabled"), true ) << "the row is as it was";
		Mutate( "updateFlow( id:$id, description:\"still stopped\" )", {{"id", flow}} );
		EXPECT_FALSE( Running(flow) ) << "an edit that enables nothing starts nothing";
		EXPECT_TRUE( Has(Refused([&]{ Inject( flow ); }).What, "needs Update") ) << "a refusal of the inject says why";

		UnscopeHistGroup( group );
		EXPECT_FALSE( Running(flow) ) << "until someone with the rights deploys it again";
		EXPECT_EQ( Deploy(flow, graph).at("running"), true );
	}

	TEST_F( FlowTests, ANodeDroppedFromItsGroupStopsTheFlow ){
		let flow = CreateFlow( "dropped-node" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let itemsBefore = Items();
		let group = CreateGroup( "flow-dropped" );
		absl::Cleanup purgeGroup = [=]{ Purge( group, itemsBefore ); };
		let node = AddNode( group, RpmManual );
		auto reopen = EnforcedHistGroups();
		ASSERT_EQ( Deploy(flow, Writes(HistGroupFixture::Ex(RpmManual))).at("running"), true );
		let checks = Flows().Rechecks();
		AddNode( group, Status1 );
		ASSERT_TRUE( WaitUntil([&]{ return Flows().Rechecks()>checks; }) ) << "a group's change is a check";
		EXPECT_TRUE( Running(flow) ) << "a node that joins the group changes nothing for the one it holds";

		Mutate( "purgeHistGroupNode( id:$id )", {{"id", node}} );
		ASSERT_TRUE( WaitUntil([&]{ return !Running( flow ); }, 15s) );
		EXPECT_EQ( Fault(flow).NodeId, "w" );
		EXPECT_TRUE( Has(Fault(flow).Message, "is in no group") );
	}

	//With nodeIds enforced on the embedded OpcServer, the server answers the deployer's own read:  Read on the root admits the
	//node, and a deny of Read on the node itself, which wins over any grant another suite's role holds, refuses it.  The roles
	//and the row are found or made and taken back after, as CallTests' are.
	TEST_F( FlowTests, AnUngroupedOpcInIsItsServersToAdmit ){
		let itemsBefore = Items();
		let flow = CreateFlow( "reads-ungrouped" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); try{ WaitItems( itemsBefore ); }catch( const std::exception& ){} };//the flow's item goes a delay after its purge.
		let node = Node( Status2 );
		let graph = Reads( HistGroupFixture::Ex(Status2) );
		let sessionless = Refused( [&]{ Deploy( flow, graph ); } );
		EXPECT_EQ( sessionless.Status, EHttpStatus::BadRequest );
		EXPECT_TRUE( sessionless.What.starts_with("Node 'src':  ") && Has(sessionless.What, "this deploy has none") ) << sessionless.What;

		auto app = Server::AppClient();
		let& schema = app->ResourceSchema;
		auto& authorizer = static_cast<Server::OpcAuthorize&>( *Server::GetSchema().Authorizer );
		let user = AppClient()->UserPK();
		let enforce = [&]( bool on ){
			EnforceResource( *app, authorizer, schema, "nodeIds", on );
			if( on )
				authorizer.AssignRights( Server::GetUAServer() );
		};
		optional<Access::RolePK> reader, denied;
		absl::Cleanup open = [&]{
			try{
				enforce( false );
				if( reader )
					app->QuerySync<jvalue>( "purgeAcl( identity:{ id:$userId }, role:{ id:$roleId } )", {{"userId", user.Value}, {"roleId", reader->Value}} );
				if( denied ){//the row unenforced first:  while it is, the purge's delegated admin check wants Administer on it.
					app->QuerySync<jvalue>( "purgeRole( id:$id )", {{"id", denied->Value}} );
					EnforceResource( *app, authorizer, schema, "nodeIds", false, node.ToString() );
					app->QuerySync<jvalue>( "purgeResource( schemaName:$schema, slug:\"nodeIds\", criteria:$criteria )", {{"schema", schema}, {"criteria", node.ToString()}} );
				}
			}
			catch( const std::exception& e ){ ADD_FAILURE() << "nodeIds wasn't opened again, or the acl wasn't purged:  " << e.what(); }
		};
		enforce( false );//the grants are made while the resource is unenforced, which is when the delegated admin check passes.
		let existing = app->QuerySync( "role(slug:$slug, deleted:null){id}", {{"slug", "GatewayFlowReader"}} );
		reader = GrantRole( *app, "GatewayFlowReader", user, {{schema, "nodeIds", ERights::Read}} );
		if( !existing.empty() )
			app->QuerySync<jvalue>( "createAcl( identity:{ id:$userId }, role:{id:$roleId} )", {{"userId", user.Value}, {"roleId", reader->Value}} );
		enforce( true );
		ASSERT_TRUE( WaitUntil([&]{ return !empty( authorizer.NodeRights(node, user) & ERights::Read ); }) ) << "the reader's grant reaches the server";
		let admitted = DeployBySession( flow, graph );
		EXPECT_EQ( admitted.at("running"), true ) << serialize( admitted );
		EXPECT_TRUE( WaitUntil([&]{ return Flows().Status( flow, "src" ).Fill=="green"; }) ) << "its item is the collector client's:  " << Flows().Status( flow, "src" ).Text;

		enforce( false );
		denied = GrantRole( *app, "GatewayFlowStatus2Denied", user, {{schema, "nodeIds", ERights::None, ERights::Read, node.ToString(), "status2"}} );
		enforce( true );
		ASSERT_TRUE( WaitUntil([&]{ return empty( authorizer.NodeRights(node, user) & ERights::Read ) && !empty( authorizer.NodeRights(Node(Rpm2), user) & ERights::Read ); }) ) << "the node's deny reaches the server";
		let refused = Refused( [&]{ DeployBySession( flow, graph ); } );
		EXPECT_TRUE( Has(refused.What, "Node 'src':  ") && Has(refused.What, "its server refused the read under the deployer's own session") ) << refused.What;
		EXPECT_EQ( Versions(flow).size(), 1u ) << "the refusal wrote no version";
		EXPECT_TRUE( Running(flow) ) << "and the flow runs on as it was";
	}

	//A node by its index is spelled by its server's namespaces, so a server out of reach leaves the check unmade:  no admission
	//at a deploy, and at a start no refusal for good.  The version here is written as a deploy would have while the server
	//answered, and the host runs on a clock the test moves.
	TEST_F( FlowTests, ACheckThatCannotBeMadeIsTriedAgain ){
		const string slug{ "flowsUnreachable" };
		PurgeServerCnnctn( slug );
		let row = GetConnection( slug, "opc.tcp://127.0.0.1:6", OpcUrn() );
		absl::Cleanup purgeConnection = [&]{ PurgeServerCnnctn( slug ); };
		let flow = CreateFlow( "unchecked", false );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let reads = [&]( sv connection ){ return Graph( {N("src", "opc in", serialize(jobject{{"connection", connection}, {"node", Node(Status2).ToString()}}), {{"d"}}), N("d", "debug")} ); };
		let unchecked = Refused( [&]{ Deploy( flow, reads(slug) ); } );
		EXPECT_EQ( unchecked.Status, EHttpStatus::ServiceUnavailable );
		EXPECT_TRUE( unchecked.What.starts_with("Node 'src':  It could not be checked:") ) << unchecked.What;
		EXPECT_TRUE( Has(Refused([&]{ Deploy( flow, reads("nowhere") ); }).What, "'nowhere' is no connection of this gateway") );
		EXPECT_TRUE( Versions(flow).empty() );

		BlockAwait( DS()->Execute(FlowSql::InsertVersion(flow, 1, serialize(reads(slug)), Clock::now(), User(), "tests", {})) );
		BlockAwait( DS()->Execute(FlowSql::SetVersion(flow, 1)) );
		auto clock = ms<Hist::Tests::ManualClock>( Clock::now() );
		Flows().Stop();
		absl::Cleanup restart = []{ Flows().Stop(); Flows().Start(); };//on the system clock again, for the tests after this one.
		Flows().Start( clock );
		Mutate( "updateFlow( id:$id, enabled:true )", {{"id", flow}} );
		EXPECT_FALSE( Running(flow) );
		EXPECT_EQ( Fault(flow).NodeId, "src" );
		EXPECT_TRUE( Fault(flow).Message.starts_with("It could not be checked:") ) << Fault( flow ).Message;

		QL().QuerySync<jvalue>( "mutation updateServerConnection( id:$id, url:$url )", {{"id", row.Id}, {"url", OpcUrl()}}, {UserPK::System} );
		clock->Advance( FlowHost::RetryInterval );
		EXPECT_TRUE( WaitUntil([&]{ return Running( flow ); }, 15s) ) << "the start is tried again, and the node, which no group holds, stands on its deploy:  " << Fault( flow ).Message;
	}

	TEST_F( FlowTests, ALinkToAnotherFlowNeedsUpdateOnIt ){
		let to = CreateFlow( "link-to" ), from = CreateFlow( "link-from" );
		absl::Cleanup purge = [=]{ PurgeFlow( from ); PurgeFlow( to ); };
		Deploy( to, Graph({N("from", "link in", R"({"name":"to-the-other"})", {{"d"}}), N("d", "debug")}) );
		let link = []( sv name ){ return Graph( {N("in", "inject", "{}", {{"to"}}), N("to", "link out", Ƒ(R"({{"name":"{}"}})", name))} ); };
		let nowhere = Refused( [&]{ Deploy( from, link("to-nowhere") ); } );
		EXPECT_TRUE( nowhere.What.starts_with("Node 'to':  ") && Has(nowhere.What, "No `link in` of this flow or of another is named 'to-nowhere'") ) << nowhere.What;

		auto reopen = EnforcedFlows();
		ScopeFlow( to, ERights::Read );
		let refused = Refused( [&]{ Deploy( from, link("to-the-other") ); } );
		EXPECT_EQ( refused.Status, EHttpStatus::Forbidden );
		EXPECT_TRUE( refused.What.starts_with("Node 'to':  ") && Has(refused.What, "A link to flow 'Flow link-to' needs Update on it") ) << refused.What;
		EXPECT_TRUE( Versions(from).empty() );

		UnscopeFlow( to );
		EXPECT_EQ( Deploy(from, link("to-the-other")).at("running"), true ) << "with Update on the flow it reaches";
	}

	TEST_F( FlowTests, TheContextSurvivesARestart ){
		let flow = CreateFlow( "remembers" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		Deploy( flow, Graph({
			N( "in", "inject", "{}", {{"set"}} ),
			N( "set", "change", R"({"rules":[{"t":"set","p":"last","pt":"flow","to":"payload","tot":"msg"},{"t":"set","p":"flow-tests","pt":"global","to":"payload","tot":"msg"}]})", {{"d"}} ),
			N( "probe", "inject", "{}", {{"same"}} ),
			N( "same", "switch", R"({"rules":[{"t":"eq","v":"last","vt":"flow"},{"t":"else"}]})", {{"d"}, {"d"}} ),
			N( "d", "debug" )
		}) );
		let last = [&]()->string{
			for( let& row : Contexts(flow) ){
				if( Json::AsSV(row.as_object(), "name")=="last" )
					return Json::AsString( row.as_object(), "value" );
			}
			return {};
		};
		Inject( flow, "in", jobject{{"payload", 7}} );
		ASSERT_TRUE( WaitUntil([&]{ return last()=="7"; }) ) << "a change is written a second on";
		let global = QL().QuerySync<jarray>( "flowContexts( name:\"flow-tests\" ){ flowId nodeId value }", {}, User() );
		ASSERT_EQ( global.size(), 1u ) << serialize( global );
		EXPECT_TRUE( global[0].as_object().at("flowId").is_null() ) << "the global scope is no flow's";
		EXPECT_EQ( global[0].as_object().at("value"), "7" );

		Inject( flow, "in", jobject{{"payload", 9}} );
		ASSERT_TRUE( Received(flow, "set", 2) );
		Flows().Stop();//which writes what waits.
		EXPECT_EQ( last(), "9" );
		Flows().Start();
		ASSERT_TRUE( WaitUntil([&]{ return Running( flow ); }) ) << "an enabled flow starts with the host, from its current version";
		EXPECT_EQ( Flows().Global()->Get("flow-tests"), 9 );
		Inject( flow, "probe", jobject{{"payload", 9}} );
		EXPECT_EQ( Sent(flow, "same", {1}), vector<uint>{1} ) << "the flow's context came back with it";
	}

	TEST_F( FlowTests, AContextEditReachesTheFlow ){
		let flow = CreateFlow( "edited-context" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		Deploy( flow, Graph({
			N( "probe", "inject", "{}", {{"same"}} ),
			N( "same", "switch", R"({"rules":[{"t":"eq","v":"limit","vt":"flow"},{"t":"else"}]})", {{"d"}, {"d"}} ),
			N( "d", "debug" )
		}) );
		let set = SetContext;
		let held = [&]( sv node, sv name ){ return Held( flow, node, name ); };
		let probe = [&]( vector<uint> sent, sv because ){
			Inject( flow, "probe", jobject{{"payload", 8}} );
			EXPECT_EQ( Sent(flow, "same", sent), sent ) << because;
		};

		let edited = set( flow, nullptr, "limit", 8 );
		EXPECT_EQ( edited.at("flow"), flow ) << serialize( edited );
		EXPECT_EQ( edited.at("name"), "limit" );
		EXPECT_EQ( edited.at("value"), "8" ) << "as its row holds it";
		EXPECT_EQ( held({}, "limit"), "8" ) << "the row is written before the mutation answers";
		probe( {1}, "the running flow reads it at its next get" );
		set( flow, nullptr, "limit", jobject{{"high", 9}} );
		EXPECT_EQ( held({}, "limit"), R"({"high":9})" ) << "any json";
		EXPECT_TRUE( set(flow, nullptr, "limit", nullptr).at("value").is_null() );
		EXPECT_EQ( held({}, "limit"), "" ) << "a null value takes the key away";
		probe( {1, 1}, "and the flow no longer has it" );

		Mutate( "updateFlow( id:$id, enabled:false )", {{"id", flow}} );
		ASSERT_FALSE( Running(flow) );
		set( flow, nullptr, "limit", 8 );
		EXPECT_EQ( held({}, "limit"), "8" ) << "a flow that is not running has its row alone";
		EXPECT_EQ( set(flow, "same", "mark", "a").at("node"), "same" );
		EXPECT_EQ( held("same", "mark"), "\"a\"" ) << "a node's scope, by the version's graph";
		Mutate( "updateFlow( id:$id, enabled:true )", {{"id", flow}} );
		ASSERT_TRUE( Running(flow) );
		probe( {1}, "which its next start loads" );
		set( flow, "same", "mark", "b" );
		EXPECT_EQ( held("same", "mark"), "\"b\"" );

		let unknown = Refused( [&]{ set( flow, "gone", "mark", 1 ); } );
		EXPECT_EQ( unknown.Status, EHttpStatus::BadRequest );
		EXPECT_TRUE( Has(unknown.What, "'gone' is no node of flow") );
		EXPECT_TRUE( Has(Refused([&]{ set( nullptr, "same", "mark", 1 ); }).What, "it needs flow") );
		EXPECT_TRUE( Has(Refused([&]{ Mutate( "setFlowContext( flow:$flow, name:\"limit\" ){ name }", {{"flow", flow}} ); }).What, "needs value") );
		EXPECT_EQ( Refused([&]{ set( 4'000'000'000, nullptr, "limit", 1 ); }).Status, EHttpStatus::NotFound );

		absl::Cleanup clear = [&]{ try{ set( nullptr, nullptr, "flow-tests-edit", nullptr ); }catch( const std::exception& ){} };
		set( nullptr, nullptr, "flow-tests-edit", jarray{1, 2} );
		EXPECT_EQ( serialize(Flows().Global()->Get("flow-tests-edit")), "[1,2]" ) << "the gateway's scope, every flow's";
		let global = QL().QuerySync<jarray>( "flowContexts( name:\"flow-tests-edit\" ){ flowId value }", {}, User() );
		ASSERT_EQ( global.size(), 1u ) << serialize( global );
		EXPECT_TRUE( global[0].as_object().at("flowId").is_null() );
		EXPECT_EQ( global[0].as_object().at("value"), "[1,2]" );

		auto reopen = EnforcedFlows();
		ScopeFlow( flow, ERights::Read );
		let ungranted = Refused( [&]{ set( flow, nullptr, "limit", 1 ); } );
		EXPECT_EQ( ungranted.Status, EHttpStatus::Forbidden ) << ungranted.What;
		EXPECT_EQ( held({}, "limit"), "8" ) << "Update on the flow, as an inject";
		UnscopeFlow( flow );
	}

	TEST_F( FlowTests, AGroupInNeedsAGroupTheGatewayHosts ){
		let flow = CreateFlow( "reads-group" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let reads = []( const jvalue& group ){ return Graph( {N("g", "group in", serialize(jobject{{"group", group}}), {{"d"}}), N("d", "debug")} ); };
		let unhosted = Refused( [&]{ Deploy( flow, reads("no-such-group") ); } );
		EXPECT_EQ( unhosted.Status, EHttpStatus::BadRequest );
		EXPECT_EQ( unhosted.What, "Node 'g':  Group 'no-such-group' isn't one this gateway historizes." );
		EXPECT_TRUE( Has(Refused([&]{ Deploy( flow, reads(nullptr) ); }).What, "`group` is required") );

		let itemsBefore = Items();
		let group = CreateGroup( "flow-reads" );
		absl::Cleanup purgeGroup = [=]{ Purge( group, itemsBefore ); };
		EXPECT_EQ( Deploy(flow, reads("flow-reads")).at("running"), true );
		EXPECT_EQ( Deploy(flow, reads(group)).at("running"), true ) << "by its id too";

		Purge( group, itemsBefore );
		std::move( purgeGroup ).Cancel();
		ASSERT_TRUE( WaitUntil([&]{ return !Running( flow ); }, 15s) ) << "the group's purge is a check";
		EXPECT_EQ( Fault(flow).NodeId, "g" );
		EXPECT_TRUE( Has(Fault(flow).Message, "isn't one this gateway historizes") );
	}

	//reviews-issue/448-flows-host.md #1:  a start whose statement fails leaves the flow with its fault and is tried again, and a
	//deploy whose version is written answers with it.  The context rows' table is renamed away, so the start's select fails.
	TEST_F( FlowTests, AStartThatFailsIsTriedAgain ){
		let flow = CreateFlow( "start-fails" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		auto clock = ms<Hist::Tests::ManualClock>( Clock::now() );
		Flows().Stop();
		absl::Cleanup restart = []{ Flows().Stop(); Flows().Start(); };
		Flows().Start( clock );
		ASSERT_EQ( Deploy(flow, Plain()).at("running"), true );

		auto named = Renamed( FlowTable::Contexts );
		let deployed = Deploy( flow, Plain() );
		EXPECT_EQ( deployed.at("version"), 2 ) << serialize( deployed );
		EXPECT_EQ( deployed.at("running"), false );
		ASSERT_TRUE( deployed.at("fault").is_object() ) << serialize( deployed );
		EXPECT_TRUE( Has(Json::AsString(deployed.at("fault").as_object(), "message"), "It could not be started:") );
		EXPECT_EQ( Row(flow).at("version"), 2 ) << "the version is current";
		EXPECT_TRUE( Has(Fault(flow).Message, "It could not be started:") );

		std::move( named ).Invoke();
		clock->Advance( FlowHost::RetryInterval );
		EXPECT_TRUE( WaitUntil([&]{ return Running( flow ); }, 15s) ) << Fault( flow ).Message;
	}

	//#2:  flow_purge takes the rows in one transaction, so a purge that fails leaves the flow whole, and it runs again.
	TEST_F( FlowTests, APurgeThatFailsLeavesTheFlowWhole ){
		let flow = CreateFlow( "purge-fails" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		Deploy( flow, Graph({N("in", "inject", "{}", {{"set"}}), N("set", "change", R"({"rules":[{"t":"set","p":"seen","pt":"flow","to":"payload","tot":"msg"}]})")}) );
		Inject( flow, "in", jobject{{"payload", 5}} );
		ASSERT_TRUE( WaitUntil([&]{ return !Contexts( flow ).empty(); }) );
		{
			auto named = Renamed( FlowTable::Runs );
			EXPECT_THROW( Mutate("purgeFlow( id:$id )", {{"id", flow}}), std::exception );
		}
		EXPECT_EQ( Versions(flow).size(), 1u ) << "its versions stand";
		EXPECT_EQ( Held(flow, {}, "seen"), "5" ) << "and its context";
		EXPECT_TRUE( WaitUntil([&]{ return Running( flow ); }) ) << "and it runs again:  " << Fault( flow ).Message;
	}

	//#3:  a context value whose write fails holds back none sorted after it, and once it has failed ContextTries passes in which
	//others were written, it is dropped.
	TEST_F( FlowTests, AContextValueThatFailsHoldsNoOtherBack ){
		let flow = CreateFlow( "context-fails" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		Deploy( flow, Plain() );
		{
			auto failing = FailingInserts( "fails" );
			SetContext( flow, nullptr, "fails", 1 );
			EXPECT_EQ( Held(flow, {}, "fails"), "" );
			SetContext( flow, nullptr, "later", 2 );
			EXPECT_EQ( Held(flow, {}, "later"), "2" ) << "a key sorted after the one that fails is written";
			for( uint i=1; i<FlowHost::ContextTries; ++i )
				SetContext( flow, nullptr, "later", 2+i );
		}
		SetContext( flow, nullptr, "later", 9 );
		EXPECT_EQ( Held(flow, {}, "later"), "9" );
		EXPECT_EQ( Held(flow, {}, "fails"), "" ) << "dropped, unwritten, after its tries";
	}

	//#4:  a key's case is its own, as in the flow's store:  under a server's default collation count and Count were one row.
	TEST_F( FlowTests, AContextKeyKeepsItsCase ){
		let flow = CreateFlow( "cased-context" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		Deploy( flow, Plain() );
		SetContext( flow, nullptr, "count", 1 );
		SetContext( flow, nullptr, "Count", 2 );
		EXPECT_EQ( Held(flow, {}, "count"), "1" );
		EXPECT_EQ( Held(flow, {}, "Count"), "2" );
	}

	//#6:  an index-form node is spelled through its server at the run's admission, and its re-checks stand on that spelling:  a
	//grant withdrawn while the server is out of reach still stops the flow.
	TEST_F( FlowTests, ARecheckStandsOnTheRunsSpelling ){
		const string slug{ "flowsSpelled" };
		PurgeServerCnnctn( slug );
		let row = GetConnection( slug, OpcUrl(), OpcUrn() );
		absl::Cleanup purgeConnection = [&]{ PurgeServerCnnctn( slug ); };
		let itemsBefore = Items();
		let group = Id( Mutate("createHistGroup( name:\"Collector flow-spelled\", slug:\"flow-spelled\", serverConnectionId:$connection ){ id }", {{"connection", row.Id}}) );
		absl::Cleanup purgeGroup = [=]{ Purge( group, itemsBefore ); };
		AddNode( group, RpmManual );
		let flow = CreateFlow( "spelled" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		auto reopen = EnforcedHistGroups();
		ASSERT_EQ( Deploy(flow, Graph({N("in", "inject", "{}", {{"w"}}), N("w", "opc out", serialize(jobject{{"connection", slug}, {"node", Node(RpmManual).ToString()}}))})).at("running"), true );

		QL().QuerySync<jvalue>( "mutation updateServerConnection( id:$id, url:\"opc.tcp://127.0.0.1:6\" )", {{"id", row.Id}}, {UserPK::System} );
		ScopeHistGroup( group, ERights::Read );
		ASSERT_TRUE( WaitUntil([&]{ return !Running( flow ); }, 15s) ) << "the withdrawal is checked with the server out of reach";
		EXPECT_TRUE( Has(Fault(flow).Message, "needs Update on group 'Collector flow-spelled'") ) << Fault( flow ).Message;
	}

	//#7:  a start loads what of its context is still to write over the rows:  a write that failed left the row behind it.
	TEST_F( FlowTests, AStartLoadsTheContextStillToWrite ){
		let flow = CreateFlow( "unwritten-context" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let graph = Graph({
			N( "probe", "inject", "{}", {{"same"}} ),
			N( "same", "switch", R"({"rules":[{"t":"eq","v":"limit","vt":"flow"},{"t":"else"}]})", {{"d"}, {"d"}} ),
			N( "d", "debug" )
		});
		Deploy( flow, graph );
		SetContext( flow, nullptr, "limit", 8 );
		auto failing = FailingInserts( "limit" );
		SetContext( flow, nullptr, "limit", 9 );
		EXPECT_NE( Held(flow, {}, "limit"), "9" ) << "the write failed";
		Deploy( flow, graph );
		Inject( flow, "probe", jobject{{"payload", 9}} );
		EXPECT_EQ( Sent(flow, "same", {1}), vector<uint>{1} ) << "the new run has the value its row lacks";
	}

	//#9, #12:  a flow deleted when the host starts still names its `link in`s, so one restored disabled is a link's to reach.
	TEST_F( FlowTests, ALinkInOfAFlowDeletedAtTheStartIsKnown ){
		let to = CreateFlow( "link-deleted", false ), from = CreateFlow( "link-to-deleted" );
		absl::Cleanup purge = [=]{ PurgeFlow( from ); PurgeFlow( to ); };
		Deploy( to, Graph({N("from", "link in", R"({"name":"to-the-deleted"})", {{"d"}}), N("d", "debug")}) );
		Mutate( "deleteFlow( id:$id )", {{"id", to}} );
		Flows().Stop();
		Flows().Start();
		Mutate( "restoreFlow( id:$id )", {{"id", to}} );
		ASSERT_FALSE( Running(to) );
		let link = Graph( {N("in", "inject", "{}", {{"to"}}), N("to", "link out", R"({"name":"to-the-deleted"})")} );
		EXPECT_EQ( Deploy(from, link).at("running"), true );
	}

	//Flows 3 (#449).
	//A hosted node's props are its host's to read, at the deploy:  the node is named, and nothing is written.
	TEST_F( FlowTests, AHostedNodesPropsAreReadAtTheDeploy ){
		let flow = CreateFlow( "hosted-props" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let refused = [&]( sv type, sv props ){ return Refused( [&]{ Deploy( flow, Graph({N("in", "inject", "{}", {{"n"}}), N("n", type, props)}) ); } ); };
		let source = [&]( sv type, sv props ){ return Refused( [&]{ Deploy( flow, Graph({N("n", type, props, {{"d"}}), N("d", "debug")}) ); } ); };
		let variable = refused( "variable", R"({"name":"mean"})" );
		EXPECT_EQ( variable.Status, EHttpStatus::BadRequest );
		EXPECT_TRUE( variable.What.starts_with("Node 'n':  ") && Has(variable.What, "waits for the flow server") ) << variable.What;
		EXPECT_TRUE( Has(refused("log", R"({"level":"loud"})").What, "`level` must be one of") );
		EXPECT_TRUE( Has(refused("log", R"({"level":"None"})").What, "`level` must be one of") ) << "reviews-issue/449-flows-sources-sinks.md #8:  no ELogLevel";
		EXPECT_TRUE( Has(refused("log", R"({"template":"{{#each}}"})").What, "its variables alone") );
		EXPECT_TRUE( Has(refused("http request", R"({"url":"ftp://example.org/"})").What, "is not an http or https url") );
		EXPECT_TRUE( Has(refused("http request", R"({"url":"http://hooks.example.org@example.com/"})").What, "carries a user") );
		EXPECT_TRUE( Has(refused("http request", R"({"url":"http://example.org/hook"})").What, "`url`'s host 'example.org' is not one /gateway/flows/http/allow lets a flow reach") ) << "the early word:  the gate is the send";
		EXPECT_TRUE( Has(refused("http request", R"({"method":"FETCH"})").What, "is not an http method") );
		EXPECT_TRUE( Has(source("timer", "{}").What, "A timer takes `every`") );
		EXPECT_TRUE( Has(source("timer", R"({"every":"PT1M","at":{"times":["06:00"]}})").What, "not both") );
		EXPECT_TRUE( Has(source("timer", R"({"at":{"times":["25:00"]}})").What, "is not a time of day") );
		EXPECT_TRUE( Has(source("timer", R"({"at":{"times":["06:00"],"zone":"Mars/Olympus"}})").What, "is no time zone the gateway knows") );
		EXPECT_TRUE( Has(source("calendar", R"({"calendar":"no-such-calendar"})").What, "is no calendar the gateway holds") );
		EXPECT_TRUE( Has(source("connection", "{}").What, "`connection` is required") );
		EXPECT_TRUE( Has(source("connection", R"({"connection":"nowhere"})").What, "'nowhere' is no connection of this gateway") );
		EXPECT_TRUE( Has(source("opc in", serialize(jobject{{"connection", OpcServerSlug}, {"node", HistGroupFixture::Ex(RpmManual)}, {"filter", jobject{{"deadbandType", "wide"}}}})).What, "`deadbandType` must be one of none, absolute, percent") );
		EXPECT_TRUE( Versions(flow).empty() );
	}

	//`opc in` on a pump's motorRpm through a `switch` into a `log`.  The item is the group's collector's, which the flow's
	//listener joins, so the node's value now is read once, where a new item's first value would come.
	TEST_F( FlowTests, AnOpcInRunsThroughASwitchIntoALog ){
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		const OneNodeGroup made{ "flow-opc-in", RpmManual };
		let flow = CreateFlow( "opc-in" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let base = std::floor( BaseRpm() );
		Deploy( flow, Graph({
			N( "src", "opc in", On(HistGroupFixture::Ex(RpmManual)), {{"sw"}} ),
			N( "sw", "switch", serialize(jobject{{"rules", jarray{jobject{{"t", "gt"}, {"v", base+500}}}}}), {{"line"}} ),
			Line( "rpm {{payload}} of {{topic}} on {{node.connection}}" )
		}) );
		ASSERT_TRUE( Received(flow, "sw", 1) ) << "the node's value now:  " << Flows().Status( flow, "src" ).Text;
		EXPECT_TRUE( WaitUntil([&]{ return Flows().Status( flow, "src" )==Flows::NodeStatus{"green", "dot", "subscribed"}; }) );
		let t0 = Since();
		WriteRpm( base+100, t0 );
		WriteRpm( base+1000.25, t0+10ms );
		ASSERT_TRUE( Received(flow, "sw", 3) ) << "each change is a message";
		ASSERT_TRUE( Received(flow, "line", 1) ) << "the switch took the one over its limit";
		ASSERT_TRUE( WaitUntil([&]{ return !Lines( capture, "opc-in" ).empty(); }) ) << "a message is counted in before its node has it";
		EXPECT_EQ( Lines(capture, "opc-in"), vector<string>{Ƒ("rpm {} of nsu={};i={} on {}", base+1000.25, PumpsUri, RpmManual, OpcServerSlug)} );
	}

	//`opc out`:  the message's value written to the node on the collector's client, the answer on the message it sends on, and
	//the write in the log.  What does not convert toward the node's DataType is the node's error.
	TEST_F( FlowTests, AnOpcOutWritesItsNode ){
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		const OneNodeGroup made{ "flow-opc-out", RpmManual };
		auto reopen = EnforcedHistGroups();
		let flow = CreateFlow( "opc-out" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("in", "inject", "{}", {{"w"}}), N("w", "opc out", On(HistGroupFixture::Ex(RpmManual)), {{"line"}}), Line("{{status}}")})).at("running"), true );
		let value = std::floor( BaseRpm() )+0.5;
		Inject( flow, "in", jobject{{"payload", value}} );
		ASSERT_TRUE( WaitUntil([&]{ return !Lines( capture, "opc-out" ).empty(); }) ) << "the answer goes on:  " << Flows().Status( flow, "w" ).Text;
		EXPECT_EQ( Current(RpmManual).AsNumber<double>(), value ) << "a read agrees";
		EXPECT_EQ( Lines(capture, "opc-out"), vector<string>{"0"} ) << "with its status, Good";
		EXPECT_EQ( Flows().Status(flow, "w"), (Flows::NodeStatus{"green", "dot", "Good"}) );
		let audited = capture.Find( [&]( const Logging::Entry& e ){ let m = e.Message(); return m.starts_with( "[opc-out.w]wrote" ) && m.contains( Ƒ("{}", value) ) && m.contains( "Good" ); } );
		EXPECT_EQ( audited.size(), 1u ) << "the write is logged with its answer";

		Inject( flow, "in", jobject{{"payload", jobject{{"not", "a number"}}}} );
		ASSERT_TRUE( Errors(flow, "w", 1) ) << "what does not convert is the node's error";
		EXPECT_EQ( Current(RpmManual).AsNumber<double>(), value );
		for( uint i=1; i<=5; ++i )
			Inject( flow, "in", jobject{{"payload", value+i}} );
		ASSERT_TRUE( Received(flow, "line", 6) );
		EXPECT_EQ( Current(RpmManual).AsNumber<double>(), value+5 ) << "one write at a time, in the order the messages came";
	}

	//A `group in` from a time replays the group's records in order and then takes each as it is stored.  A redeploy resumes
	//from its mark less the lateness, which reads the records again and sends none twice, and a record of another member
	//stamped before the mark, stored while the flow was stopped, is in that lookback and comes once.
	TEST_F( FlowTests, AGroupInReplaysFromATimeAndResumesFromItsMark ){
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		let itemsBefore = Items();
		let group = CreateGroup( "flow-replay" );
		absl::Cleanup purgeGroup = [=]{ Purge( group, itemsBefore ); };
		let rpm = AddNode( group, RpmManual ), status = AddNode( group, Status1 );
		let host = Host( group );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		WaitStored( host, rpm, Joined, 1 );
		WaitStored( host, status, Joined, 1 );
		let base = std::floor( BaseRpm() );
		let t0 = Since();
		for( uint i=0; i<3; ++i )
			WriteRpm( base+100*(i+1), t0+milliseconds{10*i} );
		WaitStored( host, rpm, t0, 3 );

		let flow = CreateFlow( "replay" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let graph = Graph( {N("src", "group in", serialize(jobject{{"group", group}, {"from", ToIsoString(t0)}, {"lateness", "PT1M"}}), {{"line"}}), Line("{{payload}}")} );
		ASSERT_EQ( Deploy(flow, graph).at("running"), true );
		let values = [&]{ return Lines( capture, "replay" ); };
		let text = []( double value ){ return Ƒ( "{}", value ); };
		ASSERT_TRUE( WaitUntil([&]{ return values().size()>=3; }) ) << Flows().Status( flow, "src" ).Text;
		EXPECT_EQ( values(), (vector<string>{text(base+100), text(base+200), text(base+300)}) ) << "the group's records from the time, in order";
		WriteRpm( base+400, t0+30ms );
		ASSERT_TRUE( WaitUntil([&]{ return values().size()>=4; }) ) << "then each as it is stored";
		EXPECT_TRUE( WaitUntil([&]{ return Held( flow, "src", "mark" )==serialize( jvalue{ToIsoString(t0+30ms)} ); }) ) << "the mark is the last record's time:  " << Held( flow, "src", "mark" );

		ASSERT_EQ( Deploy(flow, graph).at("running"), true );
		WriteRpm( base+500, t0+40ms );
		ASSERT_TRUE( WaitUntil([&]{ return values().size()>=5; }) );
		EXPECT_EQ( values().size(), 5u ) << "the redeploy read the four again, and sent none";
		EXPECT_EQ( values().back(), text(base+500) );

		Mutate( "updateFlow( id:$id, enabled:false )", {{"id", flow}} );
		ASSERT_FALSE( Running(flow) );
		let on = !*(const UA_Boolean*)Current( Status1 ).value.data;
		WriteStatus( on, t0+20ms );//before the mark, and stored after the flow last ran.
		WaitStored( host, status, t0, 1 );
		Mutate( "updateFlow( id:$id, enabled:true )", {{"id", flow}} );
		ASSERT_TRUE( WaitUntil([&]{ return values().size()>=6; }) ) << "a late record within the lookback is delivered";
		EXPECT_EQ( values().back(), on ? "true" : "false" );
		ASSERT_EQ( Deploy(flow, graph).at("running"), true );
		WriteRpm( base+600, t0+50ms );
		ASSERT_TRUE( WaitUntil([&]{ return values().size()>=7; }) );
		EXPECT_EQ( values().size(), 7u ) << "once";
		EXPECT_EQ( values().back(), text(base+600) );
	}

	TEST_F( FlowTests, ATimerTicks ){
		let flow = CreateFlow( "ticks" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("t", "timer", R"({"every":"PT0.05S"})", {{"d"}}), N("d", "debug")})).at("running"), true );
		EXPECT_TRUE( WaitUntil([&]{ return Counts( flow, "d" ).Received>=3; }) ) << Counts( flow, "d" ).Received;
	}

	//`every` ticks at the multiples of its duration from the epoch, so on the hour's quarters, and `at` at its local times in
	//its zone:  06:00 in New York, daylight time since the day before, is 10:00 UTC, and 14:30 is 18:30.
	TEST_F( FlowTests, ATimerIsAlignedAndInItsZone ){
		using namespace std::chrono;
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		let monday = sys_days{ 2026y/March/9 };
		auto [clock, restart] = OnClock( monday+10h+7min );
		let flow = CreateFlow( "aligned" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({
			N( "quarter", "timer", R"({"every":"PT15M"})", {{"line"}} ),
			N( "shift", "timer", R"({"at":{"times":["14:30","06:00"],"days":["mon"],"zone":"America/New_York"}})", {{"line"}} ),
			Line( "{{topic}} {{payload}}" )
		})).at("running"), true );
		let lines = [&]{ return Lines( capture, "aligned" ); };
		clock->Advance( 7min );
		EXPECT_TRUE( lines().empty() ) << "10:14 is no quarter";
		clock->Advance( 1min );
		ASSERT_TRUE( WaitUntil([&]{ return lines().size()==1; }) );
		EXPECT_EQ( lines()[0], Ƒ("quarter {}", ToIsoString(TimePoint{monday+10h+15min})) );
		clock->AdvanceTo( monday+18h+30min );
		let shift = Ƒ( "shift {}", ToIsoString(TimePoint{monday+18h+30min}) );
		EXPECT_TRUE( WaitUntil([&]{ return std::ranges::count( lines(), shift )==1; }) ) << "06:00 had passed, and 14:30 there is 18:30 UTC";
	}

	//A calendar's shift on the weekdays from 06:00 for eight hours, in New York:  its start and its end are the node's messages.
	TEST_F( FlowTests, ACalendarNodeSendsItsBoundaries ){
		using namespace std::chrono;
		namespace Calendars = Calendar::Client::Calendars;
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		let app = AppClient();
		for( let& v : app->QuerySync<jarray>("calendars( slug:\"flow-shifts\" ){ id }", {}) )
			app->QuerySync<jvalue>( Ƒ("purgeCalendar( id:{} )", QL::AsId<uint32>(v)), {} );
		let calendar = QL::AsId<uint32>( app->QuerySync<jobject>("createCalendar( name:\"flow-shifts\", slug:\"flow-shifts\", timeZone:\"America/New_York\", dayStart:360 ){ id }", {}) );
		absl::Cleanup purgeCalendar = [=]{ try{ AppClient()->QuerySync<jvalue>( Ƒ("purgeCalendar( id:{} )", calendar), {} ); }catch( const std::exception& ){} };
		sp<const Calendar::Schedule> held;
		ASSERT_TRUE( WaitUntil([&]{ held = Calendars::Find( calendar ); return held && held->Patterns().size()==1; }) ) << "the gateway's cache holds the calendar";
		app->QuerySync<jobject>( Ƒ("createCalendarShift( calendarPatternId:{}, name:\"Day\", days:31, start:0, minutes:480 ){{ id }}", held->Patterns().front().Id), {} );
		ASSERT_TRUE( WaitUntil([&]{ held = Calendars::Find( calendar ); return held && held->Patterns().size()==1 && held->Patterns().front().Shifts.size()==1; }) );

		let monday = sys_days{ 2026y/March/9 };
		auto [clock, restart] = OnClock( monday+9h );
		let flow = CreateFlow( "shifts" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("cal", "calendar", R"({"calendar":"flow-shifts"})", {{"line"}}), Line("{{payload}} {{occurrence.name}} {{topic}}")})).at("running"), true );
		EXPECT_EQ( Flows().Status(flow, "cal").Text, Ƒ("next {}", ToIsoString(TimePoint{monday+10h})) );
		let lines = [&]{ return Lines( capture, "shifts" ); };
		clock->AdvanceTo( monday+10h );
		ASSERT_TRUE( WaitUntil([&]{ return lines().size()==1; }) );
		EXPECT_EQ( lines()[0], "shiftStart Day flow-shifts" );
		clock->AdvanceTo( monday+18h );
		ASSERT_TRUE( WaitUntil([&]{ return lines().size()==2; }) );
		EXPECT_EQ( lines()[1], "shiftEnd Day flow-shifts" );
	}

	//A link's message reaches the other flow's `link in` as a clone, under the sender's deployer's Update on that flow, asked
	//at each message:  one who loses it stops delivering, and the refusal is the sender's node's error.
	TEST_F( FlowTests, ALinkCrossesTwoFlows ){
		let to = CreateFlow( "link-taker" ), from = CreateFlow( "link-sender" );
		absl::Cleanup purge = [=]{ PurgeFlow( from ); PurgeFlow( to ); };
		Deploy( to, Graph({N("from", "link in", R"({"name":"across"})", {{"d"}}), N("d", "debug")}) );
		Deploy( from, Graph({N("in", "inject", "{}", {{"to"}}), N("to", "link out", R"({"name":"across"})")}) );
		Inject( from, "in", jobject{{"payload", 5}} );
		EXPECT_TRUE( Received(to, "d", 1) ) << "the other flow's `link in` sends it on";
		EXPECT_EQ( Sent(to, "from", {1}), vector<uint>{1} );
		EXPECT_TRUE( WaitUntil([&]{ return Flows().Status( from, "to" ).Text=="1 flow"; }) );

		Mutate( "updateFlow( id:$id, enabled:false )", {{"id", to}} );
		Inject( from );
		EXPECT_TRUE( WaitUntil([&]{ return Flows().Status( from, "to" ).Fill=="grey"; }) ) << "a flow that is not running takes none";
		EXPECT_EQ( Counts(from, "to").Errors, 0u ) << "which is no error";
		Mutate( "updateFlow( id:$id, enabled:true )", {{"id", to}} );

		auto reopen = EnforcedFlows();
		ScopeFlow( to, ERights::Read );
		Inject( from );
		ASSERT_TRUE( Errors(from, "to", 1) ) << "the deployer lost Update on the flow it reaches";
		EXPECT_TRUE( Has(Flows().Status(from, "to").Text, "A link to flow 'Flow link-taker' needs Update on it") ) << Flows().Status( from, "to" ).Text;
		EXPECT_EQ( Counts(to, "d").Received, 0u ) << "and nothing is delivered";
		UnscopeFlow( to );
		Inject( from );
		EXPECT_TRUE( Received(to, "d", 1) ) << "with the right back, the next is";
	}

	//An `http request` reaches a host /gateway/flows/http/allow holds and no other, the list read at each send:  empty, none;
	//a redirect is followed to a host on it, and one to a host off it ends the request with the redirect as its answer.
	TEST_F( FlowTests, AnHttpRequestReachesOnlyTheHostsAllowed ){
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		TestWeb web;
		absl::Cleanup none = []{ try{ Allow( {} ); }catch( const std::exception& ){} };
		Allow( {} );
		let flow = CreateFlow( "requests" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("in", "inject", "{}", {{"http"}}), N("http", "http request", R"({"output":"json"})", {{"line"}}), Line("{{statusCode}} {{payload.method}} {{payload.body}}")})).at("running"), true );
		let request = [&]( string url, jobject message={} ){
			message["url"] = move( url );
			Inject( flow, "in", move(message) );
		};
		let lines = [&]{ return Lines( capture, "requests" ); };
		let status = [&]{ return Flows().Status( flow, "http" ).Text; };

		request( web.Url("127.0.0.1", "/ok") );
		ASSERT_TRUE( Errors(flow, "http", 1) ) << "an empty list allows none";
		EXPECT_TRUE( Has(status(), "'127.0.0.1' is not a host /gateway/flows/http/allow lets a flow reach") ) << status();
		EXPECT_EQ( web.Requests, 0u ) << "nothing was sent";

		Allow( {"hooks.example.org", "127.0.0.1"} );
		request( web.Url("127.0.0.1", "/ok") );
		ASSERT_TRUE( WaitUntil([&]{ return lines().size()==1; }) ) << status();
		EXPECT_EQ( lines()[0], "200 GET " );
		request( web.Url("127.0.0.1", "/ok"), {{"method", "post"}, {"payload", jobject{{"rpm", 1450}}}} );
		ASSERT_TRUE( WaitUntil([&]{ return lines().size()==2; }) ) << status();
		EXPECT_EQ( lines()[1], R"(200 POST {"rpm":1450})" ) << "the payload is the body";

		request( web.Url("127.0.0.1", "/redirect?to="+web.Url("127.0.0.1", "/ok")) );
		ASSERT_TRUE( WaitUntil([&]{ return lines().size()==3; }) ) << "a redirect to a host on the list is followed:  " << status();
		EXPECT_EQ( web.Requests, 4u );
		request( web.Url("127.0.0.1", "/redirect?to="+web.Url("localhost", "/ok")) );
		ASSERT_TRUE( Errors(flow, "http", 2) ) << "and one to a host off it is not";
		EXPECT_TRUE( Has(status(), "(302) It was redirected to 'http://localhost:") ) << "with the redirect as its answer";
		EXPECT_EQ( web.Requests, 5u ) << "the redirect was asked for, and its target never";
		EXPECT_EQ( lines().size(), 3u );

		Allow( {"local*"} );
		request( web.Url("localhost", "/ok") );
		EXPECT_TRUE( WaitUntil([&]{ return lines().size()==4; }) ) << "a pattern's match:  " << status();
		request( web.Url("127.0.0.1", "/ok") );
		EXPECT_TRUE( Errors(flow, "http", 3) ) << "the list is read at each send";
	}

	//A `connection` node hears its connection's loss and its return, on a flow with no `opc in`.  A row of its own:  another
	//client of the suite's slug, still up, would make the loss no outage.
	TEST_F( FlowTests, AConnectionNodeSeesTheLossAndTheReturn ){
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		const string slug{ "flowsConnection" };
		PurgeServerCnnctn( slug );
		let row = GetConnection( slug, OpcUrl(), OpcUrn() );
		absl::Cleanup purgeConnection = [&]{ PurgeServerCnnctn( slug ); };
		let itemsBefore = Items();
		let group = Id( Mutate("createHistGroup( name:\"Collector flow-connection\", slug:\"flow-connection\", serverConnectionId:$connection ){ id }", {{"connection", row.Id}}) );
		absl::Cleanup purgeGroup = [=]{ Purge( group, itemsBefore ); };
		AddNode( group, Status1 );
		ASSERT_NO_THROW( WaitSubscribed(Host(group), 1) );
		let credential = HistHost::Instance().Collector( slug );
		auto collector = UAClient::Find( slug, credential );
		ASSERT_TRUE( collector ) << "the group's collector is the slug's client";

		let flow = CreateFlow( "connection" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("c", "connection", serialize(jobject{{"connection", slug}}), {{"line"}}), Line("{{topic}} {{payload.connected}}")})).at("running"), true );
		EXPECT_EQ( Flows().Status(flow, "c").Text, "connected" );
		let lines = [&]{ return Lines( capture, "connection" ); };
		let lost = collector->Handle();
		UAClient::ConnectionLost( move(collector) );
		ASSERT_TRUE( WaitUntil([&]{ return !lines().empty(); }) ) << "the loss";
		EXPECT_EQ( lines()[0], "flowsConnection false" );
		ASSERT_TRUE( WaitRevived(lost, slug) ) << "the reconnect";
		ASSERT_TRUE( WaitUntil([&]{ return lines().size()==2; }) ) << "and the return";
		EXPECT_EQ( lines()[1], "flowsConnection true" );
		EXPECT_EQ( Flows().Status(flow, "c").Text, "connected" );
	}

	//reviews-issue/449-flows-sources-sinks.md #1:  a `group in`'s mark is saved once the flow has finished what it sent, here
	//held by a `delay` on the flow's clock, not as it sends it:  a restart before then would read the record again.
	TEST_F( FlowTests, AGroupInsMarkWaitsForTheFlowToFinish ){
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		const OneNodeGroup made{ "flow-mark", RpmManual };
		let t0 = Since();
		WriteRpm( std::floor(BaseRpm())+100, t0 );
		WaitStored( made.Host, made.Index, t0, 1 );
		auto [clock, restart] = OnClock( Clock::now() );
		let flow = CreateFlow( "mark-waits" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({
			N( "src", "group in", serialize(jobject{{"group", made.Id}, {"from", ToIsoString(t0)}}), {{"wait"}} ),
			N( "wait", "delay", R"({"duration":"PT10S"})", {{"line"}} ),
			Line( "{{payload}}" )
		})).at("running"), true );
		ASSERT_TRUE( Received(flow, "wait", 1) ) << Flows().Status( flow, "src" ).Text;
		let mark = serialize( jvalue{ToIsoString(t0)} );
		clock->Advance( 2*FlowHost::ContextDelay );//past the context's debounce, short of the delay.
		EXPECT_FALSE( WaitUntil([&]{ return !Held( flow, "src", "mark" ).empty(); }, 1s) ) << "the delay holds the record:  " << Held( flow, "src", "mark" );
		clock->Advance( 10s );
		ASSERT_TRUE( WaitUntil([&]{ return Lines( capture, "mark-waits" ).size()==1; }) );
		EXPECT_TRUE( WaitUntil([&]{ clock->Advance( FlowHost::ContextDelay ); return Held( flow, "src", "mark" )==mark; }) ) << Held( flow, "src", "mark" );
	}

	//#2:  a `group in` member that does not resolve leaves the node's status saying so, not the flow stopped, and it is
	//subscribed once the group holds it:  at a start, a member by its index waits for the collector's namespaces.
	TEST_F( FlowTests, AMemberNotYetInItsGroupIsReadOnceItIs ){
		const OneNodeGroup made{ "flow-members", RpmManual };
		let flow = CreateFlow( "member-later" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let deployed = Deploy( flow, Graph({N("src", "group in", serialize(jobject{{"group", made.Id}, {"members", jarray{HistGroupFixture::Ex(Status2)}}}), {{"d"}}), N("d", "debug")}) );
		EXPECT_EQ( deployed.at("running"), true ) << serialize( deployed );
		let status = [&]{ return Flows().Status( flow, "src" ); };
		EXPECT_EQ( status().Fill, "red" );
		EXPECT_TRUE( Has(status().Text, "is no member of its group") );
		AddNode( made.Id, Status2 );
		EXPECT_TRUE( WaitUntil([&]{ return status().Fill=="green"; }) ) << "the group's change asks for it again:  " << status().Text;
	}

	//#3:  a node that cannot be made at a start, a calendar the gateway does not hold, leaves the flow with its fault, and the
	//start is tried again RetryInterval on:  the deploy read the node's props, so what it lacked may come back.
	TEST_F( FlowTests, AStartWhoseNodeCannotBeMadeIsTriedAgain ){
		namespace Calendars = Calendar::Client::Calendars;
		let app = AppClient();
		let purgeCalendars = [=]{
			for( let& v : app->QuerySync<jarray>("calendars( slug:\"flow-later\" ){ id }", {}) )
				app->QuerySync<jvalue>( Ƒ("purgeCalendar( id:{} )", QL::AsId<uint32>(v)), {} );
		};
		let create = [&]{
			let id = QL::AsId<uint32>( app->QuerySync<jobject>("createCalendar( name:\"flow-later\", slug:\"flow-later\", timeZone:\"America/New_York\", dayStart:360 ){ id }", {}) );
			EXPECT_TRUE( WaitUntil([&]{ return Calendars::Find( id )!=nullptr; }) ) << "the gateway's cache holds the calendar";
		};
		purgeCalendars();
		absl::Cleanup purgeCalendar = [=]{ try{ purgeCalendars(); }catch( const std::exception& ){} };
		create();
		auto [clock, restart] = OnClock( Clock::now() );
		let flow = CreateFlow( "calendar-later" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("cal", "calendar", R"({"calendar":"flow-later"})", {{"d"}}), N("d", "debug")})).at("running"), true );
		Mutate( "updateFlow( id:$id, enabled:false )", {{"id", flow}} );
		purgeCalendars();
		ASSERT_TRUE( WaitUntil([&]{ return !Calendars::Find( sv{"flow-later"} ); }) );
		Mutate( "updateFlow( id:$id, enabled:true )", {{"id", flow}} );
		EXPECT_FALSE( Running(flow) );
		EXPECT_EQ( Fault(flow).NodeId, "cal" );
		EXPECT_TRUE( Has(Fault(flow).Message, "is no calendar the gateway holds") );

		create();
		clock->Advance( FlowHost::RetryInterval );
		EXPECT_TRUE( WaitUntil([&]{ return Running( flow ); }, 15s) ) << Fault( flow ).Message;
	}

	//#5:  a re-check asks about the group its run reads, by its id, whatever its slug names now:  the slug given to another
	//group neither stops the flow nor lends it that group's rights.
	TEST_F( FlowTests, ARecheckAsksAboutTheGroupItsRunReads ){
		let itemsBefore = Items();
		let first = CreateGroup( "flow-renamed" );
		absl::Cleanup purgeFirst = [=]{ Purge( first, itemsBefore ); };
		let flow = CreateFlow( "renamed-group" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		ASSERT_EQ( Deploy(flow, Graph({N("g", "group in", R"({"group":"flow-renamed"})", {{"d"}}), N("d", "debug")})).at("running"), true );
		Mutate( "updateHistGroup( id:$id, slug:\"flow-renamed-before\" )", {{"id", first}} );
		let second = Id( Mutate("createHistGroup( name:\"Collector flow-renamed after\", slug:\"flow-renamed\", serverConnectionId:$connection ){ id }", {{"connection", _connection}}) );
		absl::Cleanup purgeSecond = [=]{ Purge( second, itemsBefore ); };
		auto reopen = EnforcedHistGroups();
		ScopeHistGroup( first, ERights::None );
		ASSERT_TRUE( WaitUntil([&]{ return !Running( flow ); }, 15s) ) << "the run's own group's Read is withdrawn";
		EXPECT_TRUE( Has(Fault(flow).Message, "Group 'Collector flow-renamed' needs Read") ) << Fault( flow ).Message;
	}

	//#10:  a link asks for Update on the running flows that take its message alone:  a disabled one that holds the `link in`,
	//on which the deployer lacks it, neither refuses the message nor keeps it from the others.
	TEST_F( FlowTests, ALinkAsksOnlyAboutTheFlowsThatTakeIt ){
		let taker = CreateFlow( "link-runs" ), idle = CreateFlow( "link-idle" ), from = CreateFlow( "link-asks" );
		absl::Cleanup purge = [=]{ PurgeFlow( from ); PurgeFlow( idle ); PurgeFlow( taker ); };
		let in = Graph( {N("from", "link in", R"({"name":"to-some"})", {{"d"}}), N("d", "debug")} );
		Deploy( taker, in );
		Deploy( idle, in );
		Deploy( from, Graph({N("in", "inject", "{}", {{"to"}}), N("to", "link out", R"({"name":"to-some"})")}) );
		Mutate( "updateFlow( id:$id, enabled:false )", {{"id", idle}} );
		auto reopen = EnforcedFlows();
		ScopeFlow( idle, ERights::Read );
		Inject( from );
		EXPECT_TRUE( Received(taker, "d", 1) ) << "the running flow takes it:  " << Flows().Status( from, "to" ).Text;
		EXPECT_EQ( Counts(from, "to").Errors, 0u );
		UnscopeFlow( idle );
	}

	//Flows 4 (#450).
	//A Function node's code is compiled at the deploy, and its body runs in the sandbox the block's `function` gives its
	//flow, with what `env` names.
	TEST_F( FlowTests, AFunctionBodyRunsInItsFlowsSandbox ){
		Logging::Capture capture{ ELogTags::Flow, ELogLevel::Information };
		let flow = CreateFlow( "function" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		let body = [&]( sv code ){ return Graph( {N("in", "inject", "{}", {{"f"}}), N("f", "function", serialize(jobject{{"code", code}}), {{"line"}}), Line("{{payload}}")} ); };
		let refused = Refused( [&]{ Deploy( flow, body("const limit = 5;\nreturn msg +;") ); } );
		EXPECT_EQ( refused.Status, EHttpStatus::BadRequest );
		EXPECT_TRUE( refused.What.starts_with("Node 'f':  `code`:  SyntaxError") && Has(refused.What, "code:2") ) << refused.What;
		EXPECT_TRUE( Versions(flow).empty() ) << "a refusal writes nothing";

		Settings::Set( "/gateway/flows/env", jobject{{"SITE", "plant 3"}} );
		Settings::Set( "/gateway/flows/function", jobject{{"memory", "1MiB"}, {"timeout", "PT0.05S"}} );
		absl::Cleanup settings = []{
			Settings::Set( "/gateway/flows/env", jobject{} );
			Settings::Set( "/gateway/flows/function", jobject{} );
			Flows().Stop();
			Flows().Start();
		};
		Flows().Stop();
		Flows().Start();
		let deployed = Deploy( flow, body(
			"if( msg.payload === 'spin' )\n\tfor(;;){}\n"
			"if( msg.payload === 'fill' )\n\tfor( let last = null;; ) last = { next: last };\n"
			"msg.payload = `${msg.payload} at ${env.get( 'SITE' )} in ${env.get( 'NR_FLOW_NAME' )}`;\nreturn msg;") );
		ASSERT_EQ( deployed.at("running"), true ) << serialize( deployed );
		Inject( flow, "in", jobject{{"payload", "rpm"}} );
		ASSERT_TRUE( WaitUntil([&]{ return !Lines( capture, "function" ).empty(); }) ) << Flows().Status( flow, "f" ).Text;
		EXPECT_EQ( Lines(capture, "function"), vector<string>{"rpm at plant 3 in function"} );
		let status = [&]( sv text ){ return WaitUntil( [&]{ return Flows().Status( flow, "f" ).Text==text; } ); };//set once its drain is done, after the count.
		Inject( flow, "in", jobject{{"payload", "spin"}} );
		ASSERT_TRUE( Errors(flow, "f", 1) );
		EXPECT_TRUE( status("1 uncaught, the last:  It ran past the timeout of 50 ms.") ) << "the block's timeout:  " << Flows().Status( flow, "f" ).Text;
		Inject( flow, "in", jobject{{"payload", "fill"}} );
		ASSERT_TRUE( Errors(flow, "f", 2) );
		EXPECT_TRUE( status("2 uncaught, the last:  InternalError: out of memory") ) << "and its memory:  " << Flows().Status( flow, "f" ).Text;
	}

	//reviews-issue/450-flows-function-node.md #2:  a deploy checks the graph's Function nodes together, in one sandbox under
	//the block's limits, as the flow makes them;  and a Function node's own refusal, an On Start that throws, is not tried
	//again:  only a deploy changes it.
	TEST_F( FlowTests, AFunctionNodesRefusalIsTheDeploysAlone ){
		let flow = CreateFlow( "function-refused" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		Settings::Set( "/gateway/flows/function", jobject{{"memory", "256KiB"}} );
		auto [clock, restart] = OnClock( Clock::now() );
		absl::Cleanup settings = []{ Settings::Set( "/gateway/flows/function", jobject{} ); };//before the restart.
		let f = []( uint i ){ return N( Ƒ("f{}", i), "function", R"({"code":"return msg;"})" ); };
		let refused = Refused( [&]{ Deploy( flow, Graph({f(0), f(1), f(2), f(3), f(4)}) ); } );
		EXPECT_EQ( refused.Status, EHttpStatus::BadRequest ) << "five do not fit `function.memory` together:  " << refused.What;
		EXPECT_TRUE( Has(refused.What, "Node 'f") );
		EXPECT_EQ( Deploy(flow, Graph({f(0)})).at("running"), true ) << "one does";

		Flows().Global()->Set( "starts", nullptr );
		let deployed = Deploy( flow, Graph({N("f", "function", R"({"code":"return msg;","onStart":"global.set( 'starts', ( global.get( 'starts' ) ?? 0 ) + 1 );\nthrow new Error( 'no licence' );"})")}) );
		EXPECT_EQ( deployed.at("running"), false ) << serialize( deployed );
		EXPECT_TRUE( Has(Fault(flow).Message, "On Start:  Error: no licence") );
		EXPECT_EQ( Flows().Global()->Get("starts"), 1 );
		clock->Advance( FlowHost::RetryInterval );
		EXPECT_FALSE( WaitUntil([&]{ return Flows().Global()->Get( "starts" )!=1; }, 3s) ) << "not tried again";
		EXPECT_FALSE( Running(flow) );
	}

	//#4:  a `function.stack` over what the executor's threads leave a body is that much, and said:  a body that recurses
	//ends on its RangeError, not past its thread's stack.
	TEST_F( FlowTests, AFunctionStackIsKeptUnderTheThreads ){
		Logging::Capture capture{ ELogTags::Settings, ELogLevel::Warning };
		let flow = CreateFlow( "function-deep" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
		Settings::Set( "/gateway/flows/function", jobject{{"stack", "64MiB"}} );
		auto [clock, restart] = OnClock( Clock::now() );
		absl::Cleanup settings = []{ Settings::Set( "/gateway/flows/function", jobject{} ); };
		EXPECT_EQ( capture.Find([]( const Logging::Entry& e ){ return e.Message().find( "'/gateway/flows/function/stack' of \"64MiB\" is over the" )!=string::npos; }).size(), 1u );
		ASSERT_EQ( Deploy(flow, Graph({N("in", "inject", "{}", {{"f"}}), N("f", "function", R"({"code":"( function down(){ down(); } )();\nreturn msg;"})")})).at("running"), true );
		Inject( flow );
		ASSERT_TRUE( Errors(flow, "f", 1) );
		EXPECT_TRUE( WaitUntil([&]{ return Flows().Status( flow, "f" ).Text.find( "RangeError: Maximum call stack size exceeded" )!=string::npos; }) ) << Flows().Status( flow, "f" ).Text;
	}
}