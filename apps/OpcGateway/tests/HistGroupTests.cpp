//Historian 5b (#223):  the gateway's groups and templates (spec *Collection*, *Templates*) - the four tables, each edit's
//live side (HistGroupMAwait), the collector's items on a client of its own, and resolveHistory - against the embedded
//OpcServer's pumps nodeset:  pump1 to pump4 under the Objects folder, each with a status and a motorRpm, in urn:jde:pumps.
#include <absl/cleanup/cleanup.h>
#include <jde/fwk/utils/Stopwatch.h>
#include <jde/ql/LocalQL.h>
#include <jde/access/Authorize.h>
#include <jde/opc/UAException.h>
#include "utils/helpers.h"
#include "../src/GatewayAppClient.h"
#include "../src/hist/HistHost.h"
#include "../src/ql/GatewayQL.h"
#include "../src/UAClient.h"
#include "../src/async/ConnectAwait.h"
#include "../src/async/DataChanges.h"
#include "../src/async/Subscriptions.h"
#include "../../OpcServer/src/globals.h"
#include "../../OpcServer/src/UAServer.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using namespace std::chrono;
	namespace{
		constexpr sv PumpsUri{ "urn:jde:pumps" };
		constexpr UA_UInt32 Pump1{ 5001 }, Pump2{ 5002 }, Pump3{ 5003 }, Pump4{ 5004 }, Pump1Status{ 6011 }, Pump1Rpm{ 6012 }, Pump2Rpm{ 6022 }, Pump3Rpm{ 6032 }, Pump3Status{ 6031 }, Pump4Rpm{ 6042 };
	}

	struct HistGroupTests : ::testing::Test{
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
				GTEST_SKIP() << "HistGroupTests need the embedded OpcServer.";
			if( !HistHost::Instance().Enabled() )
				GTEST_SKIP() << "The gateway keeps no history:  /gateway/hist.";
		}
		Ω User()ι->UserPK{ return AppClient()->UserPK(); }
		//A mutation's result:  the object its result request names, or, with none, the empty list the engine answers.
		Ω Mutate( sv ql, jobject vars={} )ε->jvalue{ return BlockTAwait<jvalue>( QL::QLAwait<jvalue>{string{ql}, move(vars), User(), QLPtr()} ); }
		Ω Rows( sv ql, jobject vars={} )ε->jarray{ return QL().QuerySync<jarray>( string{ql}, move(vars), User() ); }
		Ω Id( const jvalue& v )ε->uint32{ return Json::AsNumber<uint32>( Json::AsObject(v), "id" ); }
		//The spellings the columns hold:  a node by its namespace URI, and a one-segment path from the group's root.
		Ω Node( UA_UInt32 i )ι->string{ return serialize( jobject{{"nsu", PumpsUri}, {"i", i}} ); }
		Ω Ex( UA_UInt32 i )ι->ExNodeId{ return ExNodeId{ jvalue{jobject{{"nsu", PumpsUri}, {"i", i}}} }; }
		Ω Path( sv name )ι->string{ return serialize( jarray{ jobject{{"nsu", PumpsUri}, {"name", name}} } ); }
		Ω CreateTemplate( sv slug )ε->uint32{ return Id( Mutate("createHistTemplate( name:$name, slug:$slug ){ id }", {{"name", Ƒ("Template {}", slug)}, {"slug", slug}}) ); }
		Ω CreateMember( uint32 templateId, sv name, jobject thresholds={} )ε->uint32{
			string ql{ "createHistTemplateNode( histTemplateId:$t, path:$path" };
			jobject vars{ {"t", templateId}, {"path", Path(name)} };
			for( let& [key,value] : thresholds ){
				ql += Ƒ( ", {}:${}", key, key );
				vars[key] = value;
			}
			return Id( Mutate(ql+" ){ id }", move(vars)) );
		}
		Ω CreateGroup( sv slug, optional<uint32> templateId, optional<UA_UInt32> root, jobject thresholds={} )ε->jvalue{
			string ql{ "createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection, histTemplateId:$template, root:$root" };
			jobject vars{ {"name", Ƒ("Group {}", slug)}, {"slug", slug}, {"connection", _connection}, {"template", templateId ? jvalue{*templateId} : jvalue{}}, {"root", root ? jvalue{Node(*root)} : jvalue{}} };
			for( let& [key,value] : thresholds ){
				ql += Ƒ( ", {}:${}", key, key );
				vars[key] = value;
			}
			return Mutate( ql+" ){ id guid }", move(vars) );
		}
		Ω GroupNodes( uint32 group )ε->jarray{ return Rows( "histGroupNodes( histGroupId:$group ){ id node histTemplateNodeId minTimeInterval }", {{"group", group}} ); }
		//The row of the group that holds `node`, as the tables spell it.
		Ω Row( uint32 group, UA_UInt32 node )ε->optional<jobject>{
			for( let& row : GroupNodes(group) ){
				if( Json::AsSV(row.as_object(), "node")==Node(node) )
					return row.as_object();
			}
			return nullopt;
		}
		Ω Purge( sv table, uint32 id )ι->void{
			try{
				Mutate( Ƒ("purge{}( id:$id )", table), {{"id", id}} );
			}
			catch( const std::exception& e ){
				WARNT( ELogTags::Test, "purge{}({}):  {}", table, id, e.what() );
			}
		}
		Ω Collector()ι->sp<UAClient>{ return UAClient::Find( OpcServerSlug, HistHost::Instance().Collector(OpcServerSlug) ); }
		Ω Items()ι->uint{ auto c = Collector(); return c && c->TryMonitoredNodes() ? c->TryMonitoredNodes()->Count() : 0; }
		//The server's items follow the rows after the mutation returns, on the collector's client; a drop runs a second behind.
		Ω WaitSubscribed( const sp<HistGroupHost>& host, uint count )ε->void{
			Stopwatch sw;
			while( host->Subscribed()!=count )
				sw.CheckTimeout( 15s, 10ms );
		}
		Ω WaitItems( uint count )ε->void{
			Stopwatch sw;
			while( Items()!=count )
				sw.CheckTimeout( 15s, 10ms );
		}
		//histGroupNodes enforced, as an admin may set it, with the suite's user granted nothing on it, until the cleanup returned runs
		//or is invoked.  `deleted` is the enforcement switch, so restore = enforce, as BrowseTests'
		//EnforcedConnectionResourceRefusesAnUngrantedSession has it.  Each way waits for the gateway's authorizer, which hears of the
		//change after the mutation returns:  what follows is judged under it, the test's purges and the next suite's writes included.
		Ω EnforceGroupNodes()ε{
			enforceGroupNodes( true );
			return absl::Cleanup{ []{ try{ enforceGroupNodes( false ); }catch( const std::exception& e ){ ADD_FAILURE() << "histGroupNodes wasn't opened again:  " << e.what(); } } };
		}
		Ŧ Records( const sp<HistGroupHost>& host )ε->vector<T>{
			vector<T> y;
			for( auto& record : host->Group()->Buffer() ){
				if( auto p = std::get_if<T>(&record); p )
					y.push_back( move(*p) );
			}
			return y;
		}
		static ServerCnnctnPK _connection;
		static NsIndex _ns;
	private:
		Ω enforceGroupNodes( bool on )ε->void{
			let change = on ? "restore" : "delete";
			AppClient()->QuerySync<jvalue>( Ƒ("mutation {}Resource( schemaName:\"gateway\", slug:\"histGroupNodes\", criteria:null )", change), {} );
			for( uint i=0; AppClient()->Acl()->FindActiveResourcePK("gateway", "histGroupNodes", "").has_value()!=on; ++i ){
				THROW_IF( i==200, "histGroupNodes' {} didn't reach the gateway's authorizer.", change );
				std::this_thread::sleep_for( 50ms );
			}
		}
	};
	ServerCnnctnPK HistGroupTests::_connection{};
	NsIndex HistGroupTests::_ns{};

	//A template applied to a root:  each member resolves to a hist_group_nodes row and a member of the library's group, with
	//a NodeAdded by the caller, its thresholds from the member then the group, and an item on the collector's client.  A path
	//that leads nowhere is left out, and resolveHistory says so for it and Good for the rest.
	TEST_F( HistGroupTests, TemplateResolvesIntoGroup ){
		let templateId = CreateTemplate( "pump-template" );
		absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
		let rpmMember = CreateMember( templateId, "motorRpm", {{"minTimeInterval", 1000}} );
		let statusMember = CreateMember( templateId, "status" );
		let missingMember = CreateMember( templateId, "noSuchNode" );

		let itemsBefore = Items();
		let created = CreateGroup( "pump1", templateId, Pump1, {{"maxTimeInterval", 60000}} );
		let groupId = Id( created );
		absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
		let guid = Json::AsString( Json::AsObject(created), "guid" );
		EXPECT_EQ( guid.size(), 36u ) << guid;
		let byGuid = Rows( "histGroups( guid:$guid ){ id guid }", {{"guid", guid}} );//a Guid column:  its text filters, and selects back the same.
		ASSERT_EQ( byGuid.size(), 1u ) << serialize( byGuid );
		EXPECT_EQ( Json::AsNumber<uint32>(byGuid[0].as_object(), "id"), groupId );
		EXPECT_EQ( Json::AsSV(byGuid[0].as_object(), "guid"), guid );

		let rpm = Row( groupId, Pump1Rpm ), status = Row( groupId, Pump1Status );
		ASSERT_TRUE( rpm && status ) << serialize( GroupNodes(groupId) );
		EXPECT_EQ( GroupNodes(groupId).size(), 2u ) << "the path that leads nowhere was left out";
		EXPECT_EQ( Json::AsNumber<uint32>(*rpm, "histTemplateNodeId"), rpmMember );
		EXPECT_EQ( Json::AsNumber<uint32>(*status, "histTemplateNodeId"), statusMember );
		let rpmIndex = Id( *rpm ), statusIndex = Id( *status );

		auto host = HistHost::Instance().Find( groupId );
		ASSERT_TRUE( host );
		EXPECT_EQ( host->Guid(), guid );
		EXPECT_EQ( host->Members().size(), 2u );
		EXPECT_EQ( host->Group()->Find(Ex(Pump1Rpm)), rpmIndex ) << "the row id is the node's index";
		EXPECT_EQ( host->Group()->Find(Ex(Pump1Status)), statusIndex );
		EXPECT_EQ( host->FindThresholds(rpmIndex)->MinTimeInterval, 1s ) << "the member's";
		EXPECT_EQ( host->FindThresholds(rpmIndex)->MaxTimeInterval, 1min ) << "the group's";
		EXPECT_EQ( host->FindThresholds(statusIndex)->MinTimeInterval, Duration::zero() ) << "the default";
		EXPECT_EQ( host->FindThresholds(statusIndex)->MaxTimeInterval, 1min );
		let added = Records<Opc::Hist::NodeAdded>( host );
		ASSERT_EQ( added.size(), 2u );
		for( let& record : added ){
			ASSERT_TRUE( record.By ) << "a caller's change carries a writer";
			EXPECT_EQ( record.By->IdentityId, User() );
			EXPECT_EQ( record.By->UserName, AppClient()->Acl()->UserName(User()) );
		}
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		auto collector = Collector();
		ASSERT_TRUE( collector ) << "the collector connects under its own credential";
		EXPECT_TRUE( collector->Credential.IsCollector() );
		EXPECT_EQ( Items(), itemsBefore+2 );

		let resolved = Mutate( "resolveHistory( group:$group ){ nodes{ templateNode path node status } }", {{"group", groupId}} );
		let& nodes = Json::AsArray( Json::AsObject(resolved), "nodes" );
		ASSERT_EQ( nodes.size(), 3u ) << serialize( resolved );
		for( let& j : nodes ){
			let& node = j.as_object();
			let member = Json::AsNumber<uint32>( node, "templateNode" );
			let statusCode = Json::AsNumber<StatusCode>( node, "status" );
			if( member==missingMember ){
				EXPECT_EQ( statusCode, UA_STATUSCODE_BADNOMATCH ) << serialize( node );
				EXPECT_TRUE( node.at("node").is_null() );
				EXPECT_EQ( Json::AsSV(node, "path"), Path("noSuchNode") );
			}
			else{
				EXPECT_EQ( statusCode, UA_STATUSCODE_GOOD ) << serialize( node );
				EXPECT_EQ( ExNodeId{node.at("node")}, Ex(member==rpmMember ? Pump1Rpm : Pump1Status) );
			}
		}
		EXPECT_EQ( GroupNodes(groupId).size(), 2u ) << "resolveHistory adds nothing a row already holds";
		EXPECT_THROW( Mutate("purgeHistTemplate( id:$id )", {{"id", templateId}}), std::exception ) << "a template a group uses stays";

		Mutate( "purgeHistGroup( id:$id )", {{"id", groupId}} );
		std::move( purgeGroup ).Cancel();
		EXPECT_TRUE( GroupNodes(groupId).empty() ) << "a purge takes the group's rows";
		EXPECT_FALSE( HistHost::Instance().Find(groupId) );
		EXPECT_FALSE( HistHost::Instance().Historian()->FindGroup(guid) );
		ASSERT_NO_THROW( WaitItems(itemsBefore) ) << "the items went with the group";

		Mutate( "purgeHistTemplate( id:$id )", {{"id", templateId}} );
		std::move( purgeTemplate ).Cancel();
		EXPECT_TRUE( Rows("histTemplateNodes( histTemplateId:$t ){ id }", {{"t", templateId}}).empty() ) << "a purge takes the template's members";
	}

	//A member whose create fails answers its own status, and the group's create succeeds without it:  the failure used to fail
	//the create whose hist_groups row was already written (historian-groups-templates #5).  The group's id comes by its slug, so
	//the row is purged either way;  the second is the create's failure, if any.
	Ω tryCreateGroup( sv slug, uint32 templateId, UA_UInt32 root, ServerCnnctnPK connection )ε->tuple<uint32,optional<string>>{
		optional<string> failure;
		try{
			BlockTAwait<jvalue>( QL::QLAwait<jvalue>{"createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection, histTemplateId:$template, root:$root ){ id }",
				{{"name", Ƒ("Group {}", slug)}, {"slug", slug}, {"connection", connection}, {"template", templateId}, {"root", serialize(jobject{{"nsu", PumpsUri}, {"i", root}})}}, AppClient()->UserPK(), QLPtr()} );
		}
		catch( const std::exception& e ){
			failure = e.what();
		}
		let rows = QL().QuerySync<jarray>( "histGroups( slug:$slug ){ id }", {{"slug", slug}}, UserPK{UserPK::System} );
		THROW_IF( rows.size()!=1, "'{}' has {} hist_groups rows:  {}", slug, rows.size(), failure.value_or("") );
		return { Json::AsNumber<uint32>(rows[0].as_object(), "id"), move(failure) };
	}
	Ω memberStatus( uint32 groupId )ε->StatusCode{
		let resolved = BlockTAwait<jvalue>( QL::QLAwait<jvalue>{"resolveHistory( group:$group ){ nodes{ status } }", {{"group", groupId}}, AppClient()->UserPK(), QLPtr()} );
		let& nodes = Json::AsArray( Json::AsObject(resolved), "nodes" );
		THROW_IF( nodes.size()!=1, "{} members answered, not 1:  {}", nodes.size(), serialize(resolved) );
		return Json::AsNumber<StatusCode>( nodes[0].as_object(), "status" );
	}
	//Refused:  histGroupNodes enforced and histGroups not, as an admin may set them, and the suite's user granted nothing on either.
	TEST_F( HistGroupTests, ARefusedMemberIsItsStatus ){
		let templateId = CreateTemplate( "refused-template" );
		absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
		CreateMember( templateId, "motorRpm" );
		auto open = EnforceGroupNodes();

		let [groupId, failure] = tryCreateGroup( "refused", templateId, Pump1, _connection );
		absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
		EXPECT_FALSE( failure ) << "the member's refusal failed the group's create:  " << failure.value_or( "" );
		EXPECT_EQ( memberStatus(groupId), UA_STATUSCODE_BADUSERACCESSDENIED );
		std::move( open ).Invoke();
		EXPECT_TRUE( GroupNodes(groupId).empty() );
	}
	//A duplicate:  the member resolves to a node another group already holds.
	TEST_F( HistGroupTests, AMemberAnotherGroupHoldsIsNodeIdExists ){
		let itemsBefore = Items();
		{
			let holderId = Id( CreateGroup("holder", nullopt, nullopt) );
			absl::Cleanup purgeHolder = [=]{ Purge( "HistGroup", holderId ); };
			Mutate( "createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", holderId}, {"node", Node(Pump3Rpm)}} );
			let templateId = CreateTemplate( "held-template" );
			absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
			CreateMember( templateId, "motorRpm" );

			let [groupId, failure] = tryCreateGroup( "held", templateId, Pump3, _connection );
			absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
			EXPECT_FALSE( failure ) << "the held node failed the group's create:  " << failure.value_or( "" );
			EXPECT_TRUE( GroupNodes(groupId).empty() );
			EXPECT_EQ( memberStatus(groupId), UA_STATUSCODE_BADNODEIDEXISTS );
		}
		ASSERT_NO_THROW( WaitItems(itemsBefore) ) << "the holder's item, which the next test would count";
	}

	//historian-groups-templates #10:  every Conflict was answered as another group's.  Two resolves of one group at once both ask
	//for a pending member, and the second's insert hits the unique index on the row the first just made:  the member resolved,
	//and both answer Good.  The member is left pending as ARefusedMemberIsItsStatus leaves it.
	TEST_F( HistGroupTests, ConcurrentResolvesAreBothGood ){
		ASSERT_NO_THROW( WaitItems(0) ) << "an earlier test's items are still going";
		{
			let templateId = CreateTemplate( "concurrent-template" );
			absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
			CreateMember( templateId, "motorRpm" );
			auto open = EnforceGroupNodes();
			let [groupId, failure] = tryCreateGroup( "concurrent", templateId, Pump2, _connection );
			absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
			ASSERT_FALSE( failure ) << *failure;
			std::move( open ).Invoke();
			ASSERT_TRUE( GroupNodes(groupId).empty() ) << "the member isn't pending";

			std::array<StatusCode,2> statuses{};
			std::array<string,2> errors;
			{
				auto resolve = [&]( uint i ){
					try{
						statuses[i] = memberStatus( groupId );
					}
					catch( const std::exception& e ){
						errors[i] = e.what();
					}
				};
				std::jthread a{ resolve, 0 }, b{ resolve, 1 };
			}
			for( uint i=0; i<2; ++i ){
				EXPECT_TRUE( errors[i].empty() ) << errors[i];
				EXPECT_EQ( statuses[i], UA_STATUSCODE_GOOD ) << "resolve " << i;
			}
			EXPECT_EQ( GroupNodes(groupId).size(), 1u );
			ASSERT_NO_THROW( WaitSubscribed(HistHost::Instance().Find(groupId), 1) ) << "its item, before the purge, so the wait below sees it go";
		}
		ASSERT_NO_THROW( WaitItems(0) );
	}

	//A group with no template is an ad hoc node list:  a node given by the server's namespace index is stored by its URI, joins
	//with a NodeAdded, takes its own item, keeps its thresholds through an edit, which reaches its item in place, and leaves by
	//purge with a NodeRemoved, freeing the (connection, node) index.  A second group on the connection can't hold it meanwhile.
	TEST_F( HistGroupTests, AdHocNodeJoinsAndLeaves ){
		let itemsBefore = Items();
		let groupId = Id( CreateGroup("pump2-adhoc", nullopt, nullopt) );
		absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
		auto host = HistHost::Instance().Find( groupId );
		ASSERT_TRUE( host );
		EXPECT_TRUE( host->Members().empty() );

		let nodeId = Id( Mutate("createHistGroupNode( histGroupId:$g, node:$node, minTimeInterval:500 ){ id }", {{"g", groupId}, {"node", jobject{{"ns", _ns}, {"i", Pump2Rpm}}}}) );
		auto rows = GroupNodes( groupId );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( Json::AsSV(rows[0].as_object(), "node"), Node(Pump2Rpm) ) << "spelled by URI, not the index the caller gave";
		EXPECT_EQ( host->Members().size(), 1u );
		EXPECT_EQ( host->FindThresholds(nodeId)->MinTimeInterval, 500ms );
		EXPECT_EQ( Records<Opc::Hist::NodeAdded>(host).size(), 1u );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		EXPECT_EQ( Items(), itemsBefore+1 );

		Mutate( "updateHistGroupNode( id:$id, minTimeInterval:250 )", {{"id", nodeId}} );
		EXPECT_EQ( host->FindThresholds(nodeId)->MinTimeInterval, 250ms );
		EXPECT_EQ( host->Group()->FindThresholds(nodeId)->MinTimeInterval, 250ms );
		EXPECT_THROW( Mutate("updateHistGroupNode( id:$id, node:$node )", {{"id", nodeId}, {"node", Node(Pump4Rpm)}}), std::exception ) << "a node's node never changes";
		EXPECT_THROW( Mutate("createHistGroupNode( histGroupId:$g, node:$node, exceptionDeviation:-1 ){ id }", {{"g", groupId}, {"node", Node(Pump4Rpm)}}), std::exception ) << "a threshold the library would refuse";
		//historian-threshold-overrides #3:  an infinite deviation passed Check, so the row held it before the library refused it.
		EXPECT_THROW( Mutate("updateHistGroupNode( id:$id, exceptionDeviation:$d )", {{"id", nodeId}, {"d", std::numeric_limits<double>::infinity()}}), std::exception );
		for( let& row : Rows("histGroupNodes( histGroupId:$g ){ id exceptionDeviation }", {{"g", groupId}}) ){
			if( Json::AsNumber<uint32>(row.as_object(), "id")==nodeId )
				EXPECT_TRUE( row.as_object().at("exceptionDeviation").is_null() ) << "refused before the row:  " << serialize( row );
		}
		EXPECT_THROW( Mutate("createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", groupId}, {"node", "not a node"}}), std::exception );

		let twinId = Id( CreateGroup("pump2-twin", nullopt, nullopt) );
		absl::Cleanup purgeTwin = [=]{ Purge( "HistGroup", twinId ); };
		EXPECT_THROW( Mutate("createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", twinId}, {"node", Node(Pump2Rpm)}}), std::exception ) << "a node belongs to at most one group per server";
		EXPECT_TRUE( GroupNodes(twinId).empty() );

		Mutate( "purgeHistGroupNode( id:$id )", {{"id", nodeId}} );
		EXPECT_TRUE( GroupNodes(groupId).empty() );
		EXPECT_TRUE( host->Members().empty() );
		let removed = Records<Opc::Hist::NodeRemoved>( host );
		ASSERT_EQ( removed.size(), 1u );
		EXPECT_EQ( removed[0].Index, nodeId );
		ASSERT_TRUE( removed[0].By );
		EXPECT_EQ( removed[0].By->IdentityId, User() );
		ASSERT_NO_THROW( WaitItems(itemsBefore) );

		let rejoined = Id( Mutate("createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", twinId}, {"node", Node(Pump2Rpm)}}) );
		EXPECT_GT( rejoined, nodeId ) << "a rejoin is a new row, and so a new node_index";
		EXPECT_EQ( HistHost::Instance().Find(twinId)->Group()->Find(Ex(Pump2Rpm)), rejoined );
	}

	//historian-groups-templates #7:  a threshold edit while the node's create is out.  The create had asked for the old
	//parameters, and the edit skipped the modify because the node wasn't Subscribed yet, so the item kept the old sampling.
	//The pass takes the node and submits its create in one turn of the collector's strand - DataChangeAwait's PostUA dispatches
	//inline there - so a strand task that finds the node taken holds the strand behind the create, and its answer waits.
	namespace{
		struct Joiner final : IDataChange{
			α SendDataChange( const ServerCnnctnNK&, const NodeId&, const Value& )ι->void override{}
			α to_string()Ι->string override{ return "HistGroupTests.Joiner"; }
		};
	}
	enum class EHold{ Waiting, Holding, Unstaged };
	//Unstaged:  the node was taken with no subscription to create on, so the create waits on one and isn't out yet.  The host is
	//looked up each turn, since a restore makes a new one.  No sleep between turns:  each re-post queues right behind the
	//turn that takes the node, ahead of the run_iterate that would read its create's answer.
	Ω holdOnceTaken( sp<UAClient> client, HistGroupPK groupId, NodeId node, double sampling, sp<std::atomic<EHold>> state, steady_clock::time_point until )ι->void{
		client->PostStrand( [=]()mutable{
			if( auto host = HistHost::Instance().Find(groupId); host && host->ItemParameters(node).SamplingInterval==sampling ){
				if( !client->CreatedSubscriptionResponse() )
					*state = EHold::Unstaged;
				else{
					*state = EHold::Holding;
					std::this_thread::sleep_for( 700ms );
				}
			}
			else if( steady_clock::now()<until )
				holdOnceTaken( move(client), groupId, move(node), sampling, move(state), until );
		});
	}
	TEST_F( HistGroupTests, AnEditDuringTheCreateReachesTheItem ){
		auto collector = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, HistHost::Instance().Collector(OpcServerSlug)} );
		//No group holds an item now, so an earlier test's last delete pass, which drops the subscription with its last item, is done;
		//then a subscription for the pass to find, so it takes the node and submits the create in one turn.
		ASSERT_NO_THROW( WaitItems(0) ) << "an earlier test's items are still going";
		ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{collector}) );
		{
			let groupId = Id( CreateGroup("pump4-edit", nullopt, nullopt) );
			absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
			auto host = HistHost::Instance().Find( groupId );
			ASSERT_TRUE( host );
			const NodeId node{ _ns, Pump4Rpm };
			auto state = ms<std::atomic<EHold>>( EHold::Waiting );
			holdOnceTaken( collector, groupId, node, 1000.0, state, steady_clock::now()+10s );
			let nodeId = Id( Mutate("createHistGroupNode( histGroupId:$g, node:$node, minTimeInterval:1000 ){ id }", {{"g", groupId}, {"node", Node(Pump4Rpm)}}) );
			Stopwatch sw;
			while( *state==EHold::Waiting )
				ASSERT_NO_THROW( sw.CheckTimeout(10s, 1ms) ) << "the pass never took the node";
			if( *state==EHold::Unstaged || host->Subscribed() )
				GTEST_SKIP() << "the create wasn't out when the strand was held - nothing to edit under";

			Mutate( "updateHistGroupNode( id:$id, minTimeInterval:100 )", {{"id", nodeId}} );
			ASSERT_NO_THROW( WaitSubscribed(host, 1) );
			//The item's parameters as a second listener's ack reports them:  GetResult reads what the create, then a modify, stored.
			Stopwatch modified;
			for( ;; ){
				auto joiner = ms<Joiner>();
				let ack = BlockTAwait<FromServer::SubscriptionAck>( DataChangeAwait{{node}, joiner, collector} );
				UAClient::Unsubscribe( joiner );
				ASSERT_EQ( ack.results_size(), 1 );
				if( ack.results(0).revised_sampling_interval()==100.0 ){
					EXPECT_EQ( ack.results(0).revised_queue_size(), 6u ) << "500ms of 100ms samples, plus one";
					break;
				}
				ASSERT_NO_THROW( modified.CheckTimeout(5s, 50ms) ) << "the item still samples at " << ack.results(0).revised_sampling_interval() << "ms";
			}
		}
		ASSERT_NO_THROW( WaitItems(0) );
	}

	//historian-groups-templates #9:  a pass with a node the server refused dropped the pass a member joined meanwhile asked for,
	//so that member waited RetryInterval, 30s, for its item;  now it runs at once.  A restore subscribes every row in one pass,
	//so the refused node and a good one go out together;  the strand is held behind that create, as in
	//AnEditDuringTheCreateReachesTheItem, and the member joins during the hold, after the pass took its nodes.  The good node
	//still waiting says the pass was still out.
	TEST_F( HistGroupTests, AJoinDuringARefusedPassIsNotHeldForTheRetry ){
		auto collector = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, HistHost::Instance().Collector(OpcServerSlug)} );
		ASSERT_NO_THROW( WaitItems(0) ) << "an earlier test's items are still going";
		{
			let groupId = Id( CreateGroup("pump1-refused", nullopt, nullopt) );
			absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
			constexpr UA_UInt32 missing{ 99999 };//no such node:  BadNodeIdUnknown.
			Mutate( "createHistGroupNode( histGroupId:$g, node:$node, minTimeInterval:1000 ){ id }", {{"g", groupId}, {"node", Node(missing)}} );
			Mutate( "createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", groupId}, {"node", Node(Pump1Status)}} );
			ASSERT_NO_THROW( WaitSubscribed(HistHost::Instance().Find(groupId), 1) );
			Mutate( "deleteHistGroup( id:$id )", {{"id", groupId}} );
			ASSERT_NO_THROW( WaitItems(0) );
			ASSERT_NO_THROW( BlockVoidAwait(SubscribeAwait{collector}) );

			auto state = ms<std::atomic<EHold>>( EHold::Waiting );
			holdOnceTaken( collector, groupId, NodeId{_ns, missing}, 1000.0, state, steady_clock::now()+10s );
			Mutate( "restoreHistGroup( id:$id )", {{"id", groupId}} );
			Stopwatch held;
			while( *state==EHold::Waiting )
				ASSERT_NO_THROW( held.CheckTimeout(10s, 1ms) ) << "the pass never took the node";
			auto host = HistHost::Instance().Find( groupId );
			ASSERT_TRUE( host );
			if( *state==EHold::Unstaged || host->Subscribed() )
				GTEST_SKIP() << "the restore's create wasn't out when the strand was held";

			Mutate( "createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", groupId}, {"node", Node(Pump1Rpm)}} );
			Stopwatch sw;
			while( host->Subscribed()!=2 )
				ASSERT_NO_THROW( sw.CheckTimeout(10s, 10ms) ) << "the member waited for the refused node's retry";
		}
		ASSERT_NO_THROW( WaitItems(0) );
	}

	//A deleted group's members leave and its items go; a restored one's rows join again.
	TEST_F( HistGroupTests, DeleteAndRestore ){
		let itemsBefore = Items();
		let created = CreateGroup( "pump4-delete", nullopt, nullopt );
		let groupId = Id( created );
		absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
		let nodeId = Id( Mutate("createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", groupId}, {"node", Node(Pump4Rpm)}}) );
		auto host = HistHost::Instance().Find( groupId );
		ASSERT_TRUE( host );
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );

		Mutate( "deleteHistGroup( id:$id )", {{"id", groupId}} );
		EXPECT_FALSE( HistHost::Instance().Find(groupId) );
		EXPECT_FALSE( HistHost::Instance().Historian()->FindGroup(Json::AsString(Json::AsObject(created), "guid")) );
		EXPECT_EQ( GroupNodes(groupId).size(), 1u ) << "a delete keeps the rows for the restore";
		ASSERT_NO_THROW( WaitItems(itemsBefore) );

		Mutate( "restoreHistGroup( id:$id )", {{"id", groupId}} );
		auto restored = HistHost::Instance().Find( groupId );
		ASSERT_TRUE( restored );
		EXPECT_NE( restored, host );
		EXPECT_EQ( restored->Group()->Find(Ex(Pump4Rpm)), nodeId ) << "the row keeps its index";
		ASSERT_NO_THROW( WaitSubscribed(restored, 1) );
		EXPECT_EQ( Items(), itemsBefore+1 );
	}

	//historian-groups-templates #11:  a member its template gained while the group was deleted - TemplateNodeCreated resolves live
	//groups only - stayed unresolved through the restore, until someone ran resolveHistory.
	TEST_F( HistGroupTests, ARestoreResolvesTheMembersItMissed ){
		ASSERT_NO_THROW( WaitItems(0) ) << "an earlier test's items are still going";
		{
			let templateId = CreateTemplate( "restore-template" );
			absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
			CreateMember( templateId, "status" );
			let groupId = Id( CreateGroup("pump4-restore", templateId, Pump4) );
			absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
			ASSERT_EQ( GroupNodes(groupId).size(), 1u );

			Mutate( "deleteHistGroup( id:$id )", {{"id", groupId}} );
			let rpmMember = CreateMember( templateId, "motorRpm" );
			EXPECT_EQ( GroupNodes(groupId).size(), 1u ) << "a deleted group resolves nothing";
			Mutate( "restoreHistGroup( id:$id )", {{"id", groupId}} );
			EXPECT_EQ( GroupNodes(groupId).size(), 2u ) << serialize( GroupNodes(groupId) );
			let rpm = Row( groupId, Pump4Rpm );
			ASSERT_TRUE( rpm ) << "the restore didn't resolve the member added while the group was deleted";
			EXPECT_EQ( Json::AsNumber<uint32>(*rpm, "histTemplateNodeId"), rpmMember );
			auto host = HistHost::Instance().Find( groupId );
			ASSERT_TRUE( host );
			EXPECT_EQ( host->Members().size(), 2u );
			ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		}
		ASSERT_NO_THROW( WaitItems(0) );
	}

	//A template edit reaches every group made from it:  a member added resolves in each group, one removed takes its node
	//from each, and a threshold on the member, the group or the node resolves row, then member, then group.
	TEST_F( HistGroupTests, TemplateEditsReachTheGroup ){
		let templateId = CreateTemplate( "pump3-template" );
		absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
		let rpmMember = CreateMember( templateId, "motorRpm" );
		let groupId = Id( CreateGroup("pump3", templateId, Pump3) );
		absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
		auto host = HistHost::Instance().Find( groupId );
		ASSERT_TRUE( host );
		ASSERT_EQ( GroupNodes(groupId).size(), 1u );
		let rpmRow = Id( *Row(groupId, Pump3Rpm) );

		let statusMember = CreateMember( templateId, "status", {{"maxTimeInterval", 30000}} );
		ASSERT_EQ( GroupNodes(groupId).size(), 2u ) << "a member added resolves in the template's groups";
		let statusRow = Id( *Row(groupId, Pump3Status) );
		EXPECT_EQ( host->FindThresholds(statusRow)->MaxTimeInterval, 30s );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );

		Mutate( "updateHistTemplateNode( id:$id, maxTimeInterval:45000 )", {{"id", rpmMember}} );
		EXPECT_EQ( host->FindThresholds(rpmRow)->MaxTimeInterval, 45s ) << "the member's edit reaches the node made from it";
		Mutate( "updateHistGroup( id:$id, exceptionDeviation:2 )", {{"id", groupId}} );
		EXPECT_EQ( host->FindThresholds(rpmRow)->ExceptionDeviation, 2 ) << "the group's, where the member leaves it null";
		EXPECT_EQ( host->FindThresholds(statusRow)->ExceptionDeviation, 2 );
		Mutate( "updateHistGroupNode( id:$id, exceptionDeviation:5 )", {{"id", rpmRow}} );
		EXPECT_EQ( host->FindThresholds(rpmRow)->ExceptionDeviation, 5 ) << "the row's over the group's";
		EXPECT_EQ( host->FindThresholds(statusRow)->ExceptionDeviation, 2 );
		EXPECT_EQ( host->FindThresholds(rpmRow)->MaxTimeInterval, 45s );
		EXPECT_THROW( Mutate("updateHistTemplateNode( id:$id, path:$path )", {{"id", rpmMember}, {"path", Path("status")}}), std::exception ) << "a member's path never changes";
		EXPECT_THROW( Mutate("updateHistGroup( id:$id, root:$root )", {{"id", groupId}, {"root", Node(Pump1)}}), std::exception ) << "a group's root never changes";

		Mutate( "purgeHistTemplateNode( id:$id )", {{"id", statusMember}} );
		EXPECT_EQ( GroupNodes(groupId).size(), 1u ) << "a member removed takes its node from the template's groups";
		EXPECT_FALSE( Row(groupId, Pump3Status) );
		EXPECT_FALSE( host->Group()->Find(Ex(Pump3Status)) );
		let removed = Records<Opc::Hist::NodeRemoved>( host );
		ASSERT_EQ( removed.size(), 1u );
		EXPECT_EQ( removed[0].Index, statusRow );
	}

	//historian-groups-templates #12:  every hist_groups update modified every subscribed item, one ModifyMonitoredItems each, a
	//rename included.  Now an edit sends one request for the items whose parameters it changes, and none when it changes none.
	//Counted by the embedded server's session diagnostics, every session's:  the items' parameters alone can't show it.
	Ω modifyRequests()ε->uint{
		UA_Variant v; UA_Variant_init( &v );
		UAε( UA_Server_readValue(Server::FindUAServer()->Ptr(), UA_NODEID_NUMERIC(0, UA_NS0ID_SERVER_SERVERDIAGNOSTICS_SESSIONSDIAGNOSTICSSUMMARY_SESSIONDIAGNOSTICSARRAY), &v) );
		uint y{};
		if( v.type==&UA_TYPES[UA_TYPES_SESSIONDIAGNOSTICSDATATYPE] ){
			for( size_t i=0; i<v.arrayLength; ++i )
				y += ((UA_SessionDiagnosticsDataType*)v.data)[i].modifyMonitoredItemsCount.totalCount;
		}
		UA_Variant_clear( &v );
		return y;
	}
	TEST_F( HistGroupTests, AGroupEditModifiesItsItemsOnce ){
		ASSERT_NO_THROW( WaitItems(0) ) << "an earlier test's items are still going";
		auto collector = BlockTAwait<sp<UAClient>>( ConnectAwait{string{OpcServerSlug}, HistHost::Instance().Collector(OpcServerSlug)} );
		{
			let groupId = Id( CreateGroup("pumps-edit", nullopt, nullopt) );
			absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
			for( let node : {Pump1Rpm, Pump2Rpm, Pump3Rpm} )
				Mutate( "createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", groupId}, {"node", Node(node)}} );
			auto host = HistHost::Instance().Find( groupId );
			ASSERT_TRUE( host );
			ASSERT_NO_THROW( WaitSubscribed(host, 3) );

			let before = modifyRequests();
			Mutate( "updateHistGroup( id:$id, name:$name, exceptionDeviation:2 )", {{"id", groupId}, {"name", "Pumps renamed"}} );
			std::this_thread::sleep_for( 300ms );//a request sent would be answered by now.
			EXPECT_EQ( modifyRequests(), before ) << "a rename and a deviation change no item's sampling or queue";

			Mutate( "updateHistGroup( id:$id, minTimeInterval:250 )", {{"id", groupId}} );
			Stopwatch sw;
			while( modifyRequests()==before )
				ASSERT_NO_THROW( sw.CheckTimeout(5s, 10ms) ) << "the edit modified no item";
			std::this_thread::sleep_for( 300ms );//and no second request follows.
			EXPECT_EQ( modifyRequests(), before+1 ) << "one request for the group's three items";
			auto joiner = ms<Joiner>();
			let ack = BlockTAwait<FromServer::SubscriptionAck>( DataChangeAwait{{NodeId{_ns, Pump3Rpm}}, joiner, collector} );
			UAClient::Unsubscribe( joiner );
			ASSERT_EQ( ack.results_size(), 1 );
			EXPECT_EQ( ack.results(0).revised_sampling_interval(), 250.0 ) << "the request's last item";
		}
		ASSERT_NO_THROW( WaitItems(0) );
	}

	//historian-groups-templates #13:  a member's path was stored as its caller wrote it, so one path in two spellings made two
	//members of one template, which resolved to one node.  It is stored as HistPathText writes it, and the second is the same key.
	TEST_F( HistGroupTests, APathIsStoredOneWay ){
		let templateId = CreateTemplate( "spelling-template" );
		absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
		let spelling = Ƒ( R"([ {{"name":"motorRpm", "nsu":"{}"}} ])", PumpsUri );
		let created = Mutate( "createHistTemplateNode( histTemplateId:$t, path:$path ){ id path }", {{"t", templateId}, {"path", spelling}} );
		EXPECT_EQ( Json::AsSV(Json::AsObject(created), "path"), Path("motorRpm") ) << "the reply's path";
		let rows = Rows( "histTemplateNodes( histTemplateId:$t ){ id path }", {{"t", templateId}} );
		ASSERT_EQ( rows.size(), 1u );
		EXPECT_EQ( Json::AsSV(rows[0].as_object(), "path"), Path("motorRpm") ) << "stored as its caller wrote it";
		EXPECT_THROW( CreateMember(templateId, "motorRpm"), std::exception ) << "the same path in another spelling is another member";
		EXPECT_EQ( Rows("histTemplateNodes( histTemplateId:$t ){ id }", {{"t", templateId}}).size(), 1u );
	}

	//What a create refuses before it writes:  a group with no connection, one that isn't an id or one that doesn't exist, a member
	//with no path or a format that isn't one, a node with no group.
	TEST_F( HistGroupTests, CreateRefusals ){
		EXPECT_THROW( Mutate("createHistGroup( name:$name, slug:$slug ){ id }", {{"name", "No connection"}, {"slug", "no-connection"}}), std::exception );
		for( let& connection : {jvalue{std::to_string(_connection)}, jvalue{_connection+0.5}, jvalue{-1}} ){
			try{
				Mutate( "createHistGroup( name:$name, slug:$slug, serverConnectionId:$connection, root:$root ){ id }",
					{{"name", "Bad connection"}, {"slug", "bad-connection"}, {"connection", connection}, {"root", serialize(jobject{{"ns", _ns}, {"i", Pump1}})}} );
				ADD_FAILURE() << "serverConnectionId " << serialize( connection ) << " was taken.";
			}
			catch( const Exception& e ){
				EXPECT_EQ( e.HttpStatus(), EHttpStatus::BadRequest ) << serialize( connection ) << ":  " << e.what();
			}
		}
		EXPECT_TRUE( Rows("histGroups( slug:$slug ){ id }", {{"slug", "bad-connection"}}).empty() );
		try{//historian-groups-templates N1:  a root by index spells through a connection that doesn't exist, which is a 404.
			Mutate( "createHistGroup( name:$name, slug:$slug, serverConnectionId:999999, root:$root ){ id }",
				{{"name", "No such connection"}, {"slug", "no-such-connection"}, {"root", serialize(jobject{{"ns", _ns}, {"i", Pump1}})}} );
			ADD_FAILURE() << "a connection that doesn't exist was taken";
		}
		catch( const Exception& e ){
			EXPECT_EQ( e.HttpStatus(), EHttpStatus::NotFound ) << e.what();
		}
		let templateId = CreateTemplate( "refusals" );
		absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
		EXPECT_THROW( Mutate("createHistTemplateNode( histTemplateId:$t, path:$path ){ id }", {{"t", templateId}, {"path", "motorRpm"}}), std::exception ) << "a path is a json list of {nsu, name}";
		EXPECT_THROW( Mutate("createHistTemplateNode( histTemplateId:$t, path:$path ){ id }", {{"t", templateId}, {"path", "[]"}}), std::exception );
		EXPECT_THROW( Mutate("createHistTemplateNode( histTemplateId:$t, path:$path, exceptionDeviationFormat:9 ){ id }", {{"t", templateId}, {"path", Path("motorRpm")}}), std::exception );
		//historian-groups-templates N5:  a format a uint8 can't hold was cast, undefined - 259 and 1.5 passed Check as 3 and 1 - and
		//the insert's exact conversion then refused them, and a string, as a 500.
		for( let& format : {jvalue{259}, jvalue{1.5}, jvalue{-1}, jvalue{"1"}} ){
			try{
				Mutate( "createHistTemplateNode( histTemplateId:$t, path:$path, exceptionDeviationFormat:$format ){ id }", {{"t", templateId}, {"path", Path("motorRpm")}, {"format", format}} );
				ADD_FAILURE() << "exceptionDeviationFormat " << serialize( format ) << " was taken.";
			}
			catch( const Exception& e ){
				EXPECT_EQ( e.HttpStatus(), EHttpStatus::BadRequest ) << serialize( format ) << ":  " << e.what();
			}
		}
		EXPECT_TRUE( Rows("histTemplateNodes( histTemplateId:$t ){ id }", {{"t", templateId}}).empty() );
		EXPECT_THROW( Mutate("createHistGroupNode( histGroupId:999999, node:$node ){ id }", {{"node", Node(Pump1Rpm)}}), std::exception ) << "no such group";
	}

	//A group's member list for the web (#338):  historyThresholds answers each member with its node by the server's index, its
	//template member and path, each threshold's effective value with where it comes from - the row's own, its template member's,
	//its group's or the default - and whether the caller may override them.  An override cleared back to null, by a literal or a
	//variable bound to null as the web sends it, inherits again, which the live group follows;  and the server's word on an
	//enumeration comes with the member once its item is in.
	TEST_F( HistGroupTests, ThresholdsAnswerWithSources ){
		let templateId = CreateTemplate( "threshold-sources" );
		absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
		let rpmMember = CreateMember( templateId, "motorRpm", {{"minTimeInterval", 1000}} );
		let groupId = Id( CreateGroup("pump2-thresholds", templateId, Pump2, {{"maxTimeInterval", 60000}}) );
		absl::Cleanup purgeGroup = [=]{ Purge( "HistGroup", groupId ); };
		auto host = HistHost::Instance().Find( groupId );
		ASSERT_TRUE( host );
		ASSERT_EQ( GroupNodes(groupId).size(), 1u );
		let rpmRow = Id( *Row(groupId, Pump2Rpm) );
		Mutate( "updateHistGroupNode( id:$id, exceptionDeviation:5, exceptionDeviationFormat:1 )", {{"id", rpmRow}} );
		let thresholds = []( uint32 group ){ return QL().QuerySync<jobject>( "historyThresholds( group:$group ){ update nodes{ id node templateNode path enumeration band thresholds{ name value source } } }", {{"group", group}}, User() ); };
		//A member's answer by its row, and a threshold's value and source by its name.
		let member = []( const jobject& answer, uint32 id )->jobject{
			for( let& node : Json::AsArray(answer, "nodes") ){
				if( Json::AsNumber<uint32>(node.as_object(), "id")==id )
					return node.as_object();
			}
			return {};
		};
		let threshold = []( const jobject& node, sv name )->const jobject&{
			for( let& t : Json::AsArray(node, "thresholds") ){
				if( Json::AsSV(t.as_object(), "name")==name )
					return t.as_object();
			}
			THROW( "{} isn't answered:  {}", name, serialize(node) );
		};
		let number = [&]( const jobject& node, sv name )->optional<double>{ let& v = threshold( node, name ).at( "value" ); return v.is_null() ? nullopt : optional<double>{ v.to_number<double>() }; };
		let source = [&]( const jobject& node, sv name )->string{ return string{ Json::AsSV(threshold(node, name), "source") }; };
		ASSERT_NO_THROW( WaitSubscribed(host, 1) );
		auto answer = thresholds( groupId );
		EXPECT_TRUE( Json::AsBool(answer, "update") ) << "nothing enforces hist_group_nodes";
		auto rpm = member( answer, rpmRow );
		ASSERT_FALSE( rpm.empty() ) << serialize( answer );
		EXPECT_EQ( Json::AsNumber<uint32>(rpm, "templateNode"), rpmMember );
		EXPECT_EQ( Json::AsSV(rpm, "path"), Path("motorRpm") );
		let& node = Json::AsObject( rpm, "node" );
		EXPECT_EQ( Json::AsSV(node, "nsu"), PumpsUri );
		EXPECT_EQ( Json::FindNumber<NsIndex>(node, "ns").value_or(0), _ns ) << "by the server's index, as a page keys its variables";
		EXPECT_EQ( Json::AsNumber<UA_UInt32>(node, "i"), Pump2Rpm );
		EXPECT_EQ( number(rpm, "exceptionDeviation"), 5 ); EXPECT_EQ( source(rpm, "exceptionDeviation"), "node" );
		EXPECT_EQ( number(rpm, "exceptionDeviationFormat"), 1 ); EXPECT_EQ( source(rpm, "exceptionDeviationFormat"), "node" );
		EXPECT_EQ( number(rpm, "minTimeInterval"), 1000 ); EXPECT_EQ( source(rpm, "minTimeInterval"), "template" );
		EXPECT_EQ( number(rpm, "maxTimeInterval"), 60000 ); EXPECT_EQ( source(rpm, "maxTimeInterval"), "group" );
		EXPECT_EQ( threshold(rpm, "stepped").at("value"), jvalue{true} ); EXPECT_EQ( source(rpm, "stepped"), "default" );

		Mutate( "updateHistGroupNode( id:$id, exceptionDeviation:$deviation, exceptionDeviationFormat:null )", {{"id", rpmRow}, {"deviation", jvalue{}}} );
		rpm = member( thresholds(groupId), rpmRow );
		EXPECT_FALSE( number(rpm, "exceptionDeviation") ) << "cleared, it inherits the default:  no band";
		EXPECT_EQ( source(rpm, "exceptionDeviation"), "default" );
		EXPECT_EQ( number(rpm, "exceptionDeviationFormat"), 0 ); EXPECT_EQ( source(rpm, "exceptionDeviationFormat"), "default" );
		EXPECT_FALSE( host->FindThresholds(rpmRow)->ExceptionDeviation ) << "the live group follows";
		EXPECT_EQ( rpm.at("band"), jvalue{"none"} );
		Mutate( "updateHistGroup( id:$id, exceptionDeviation:2 )", {{"id", groupId}} );
		rpm = member( thresholds(groupId), rpmRow );
		EXPECT_EQ( number(rpm, "exceptionDeviation"), 2 ); EXPECT_EQ( source(rpm, "exceptionDeviation"), "group" );
		EXPECT_EQ( host->FindThresholds(rpmRow)->ExceptionDeviation, 2 );

		//The server's word on each member's DataType, once its item is in:  motorRpm is a Double, ServerStatus.State an enumeration.
		let state = Id( Mutate("createHistGroupNode( histGroupId:$g, node:$node ){ id }", {{"g", groupId}, {"node", serialize(jobject{{"nsu", "http://opcfoundation.org/UA/"}, {"i", UA_NS0ID_SERVER_SERVERSTATUS_STATE}})}}) );
		ASSERT_NO_THROW( WaitSubscribed(host, 2) );
		Stopwatch sw;
		while( !host->Description(rpmRow).value_or(HistDescription{}).Enumeration || !host->Description(rpmRow)->Ranged || !host->Description(state).value_or(HistDescription{}).Enumeration )
			ASSERT_NO_THROW( sw.CheckTimeout(15s, 10ms) );
		answer = thresholds( groupId );
		EXPECT_EQ( member(answer, rpmRow).at("enumeration"), jvalue{false} );
		EXPECT_EQ( member(answer, state).at("enumeration"), jvalue{true} );
		EXPECT_EQ( member(answer, rpmRow).at("band"), jvalue{"applied"} ) << "the group's 2, absolute";
		EXPECT_EQ( member(answer, state).at("band"), jvalue{"enumeration"} );
		//historian-threshold-overrides #4:  a percent format whose range the node lacks stores every change, which the member says
		//where its thresholds can't.
		Mutate( "updateHistGroupNode( id:$id, exceptionDeviationFormat:3 )", {{"id", rpmRow}} );
		EXPECT_EQ( member(thresholds(groupId), rpmRow).at("band"), jvalue{"noRange"} ) << "motorRpm has no EURange";
		EXPECT_TRUE( member(answer, state).at("templateNode").is_null() ) << "added by hand";
		EXPECT_TRUE( member(answer, state).at("path").is_null() );

		//`update` is the table's right:  enforced with the suite's user granted nothing on it, the overrides are refused, and it says so.
		auto open = EnforceGroupNodes();
		EXPECT_FALSE( Json::AsBool(thresholds(groupId), "update") );
		EXPECT_THROW( Mutate("updateHistGroupNode( id:$id, stepped:false )", {{"id", rpmRow}}), std::exception );
		std::move( open ).Invoke();
		EXPECT_THROW( thresholds(999999), std::exception ) << "a group the gateway doesn't host";
	}
}