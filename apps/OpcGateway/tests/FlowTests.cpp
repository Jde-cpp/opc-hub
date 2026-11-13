//Flows 2 (#448):  the host (docs/flows/spec.md *Flows*, *Rights*, *Context*) - the four tables, each edit's live side
//(FlowMAwait), deployFlow, injectFlow and setFlowContext, the admission of a graph for its deployer at the deploy and again
//as the rights and the groups change, and the context kept across a restart - over the embedded OpcServer's pumps.  The sources and sinks
//are the next phase's:  a hosted node here is its stand-in, which is all the admission needs.
#include <fstream>
#include <absl/cleanup/cleanup.h>
#include <jde/db/meta/AppSchema.h>
#include "utils/HistGroupFixture.h"
#include "../src/flows/FlowHost.h"
#include "../../OpcServer/src/access/OpcAuthorize.h"
#include "../../../libs/historian/tests/ManualClock.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using Access::ERights;
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
		let flow = CreateFlow( "reads-ungrouped" );
		absl::Cleanup purge = [=]{ PurgeFlow( flow ); };
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
		EXPECT_EQ( Flows().Status(flow, "src").Fill, "grey" ) << "the source is the next phase's";

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
}