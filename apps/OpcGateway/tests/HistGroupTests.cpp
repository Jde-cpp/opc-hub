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
#include "../../OpcServer/src/globals.h"
#include "../../OpcServer/src/UAServer.h"
#define let const auto

namespace Jde::Opc::Gateway::Tests{
	using namespace std::chrono;
	namespace{
		constexpr sv PumpsUri{ "urn:jde:pumps" };
		constexpr UA_UInt32 Pump1{ 5001 }, Pump3{ 5003 }, Pump1Status{ 6011 }, Pump1Rpm{ 6012 }, Pump2Rpm{ 6022 }, Pump3Rpm{ 6032 }, Pump3Status{ 6031 }, Pump4Rpm{ 6042 };
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

	//What a create refuses before it writes:  a group with no connection, a member with no path, a node with no group.
	TEST_F( HistGroupTests, CreateRefusals ){
		EXPECT_THROW( Mutate("createHistGroup( name:$name, slug:$slug ){ id }", {{"name", "No connection"}, {"slug", "no-connection"}}), std::exception );
		let templateId = CreateTemplate( "refusals" );
		absl::Cleanup purgeTemplate = [=]{ Purge( "HistTemplate", templateId ); };
		EXPECT_THROW( Mutate("createHistTemplateNode( histTemplateId:$t, path:$path ){ id }", {{"t", templateId}, {"path", "motorRpm"}}), std::exception ) << "a path is a json list of {nsu, name}";
		EXPECT_THROW( Mutate("createHistTemplateNode( histTemplateId:$t, path:$path ){ id }", {{"t", templateId}, {"path", "[]"}}), std::exception );
		EXPECT_THROW( Mutate("createHistTemplateNode( histTemplateId:$t, path:$path, exceptionDeviationFormat:9 ){ id }", {{"t", templateId}, {"path", Path("motorRpm")}}), std::exception );
		EXPECT_TRUE( Rows("histTemplateNodes( histTemplateId:$t ){ id }", {{"t", templateId}}).empty() );
		EXPECT_THROW( Mutate("createHistGroupNode( histGroupId:999999, node:$node ){ id }", {{"node", Node(Pump1Rpm)}}), std::exception ) << "no such group";
	}
}